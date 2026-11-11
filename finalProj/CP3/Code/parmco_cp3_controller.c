/*
 * parmco_cp3_controller.c - PARMCO RP4 controller (Checkpoint 3)
 *
 * AI-generated. Original CP3 structure (RFCOMM server, IR thread, AS5600 module,
 * CSV logging) by the team's earlier AI sessions; this revision by Claude adds:
 *   - BLE server for the iPhone app (ble_server.c), sharing one command parser
 *     with the RFCOMM (Android) server
 *   - calibrate zero / set to zero / angle target driven by the AS5600 encoder,
 *     with a "not calibrated" state that blocks position moves
 *   - encoder RPM averaged over a 0.5 s window; IR RPM from pulse period
 *   - IR input read through the class library (E4235_Read) instead of the
 *     GPIO registers
 *   - over-range unlock (auto-off on stop), acceleration ramp, real-time motor
 *     thread using the assembly step_at_deadline() step-timing loop
 *   - 100 Hz CSV logging in its own thread with the columns from the project
 *     appendix; 5 Hz telemetry to the app; defaults re-applied on connect
 *   - optional encoder feedback hold (HOLD:ON) and basic Maintain mode
 *     (both are final-check features; not required at CP3)
 *
 * Build: make      Run: sudo ./parmco_cp3 [-i] [-H] [-s] [-l logdir]
 *   -i  invert encoder direction (if the app angle runs opposite the protractor)
 *   -H  enable feedback hold by default
 *   -s  require an encrypted (paired) BLE link
 *   -l  directory for CSV logs (default ./logs)
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include "E4235.h"
#include "as5600.h"
#include "ble_server.h"

/* ------------------------------------------------------------- assembly API */
extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int *pins);
extern void step_at_deadline(unsigned int deadline, int direction, int mode, int *pins);
extern void set_step_index(int index);
extern int  get_step_index(void);
extern unsigned int get_timer_frequency(void);
extern unsigned int get_current_timer(void);

/* ------------------------------------------------------------ configuration */
#define PIN_ENABLE        24
#define PIN_A             17
#define PIN_B             27
#define PIN_C             22
#define PIN_D             23
#define PIN_IR            13

#define STEP_MODE         0          /* 0 = full step (200/rev) */
#define STEPS_PER_REV     200
#define STEP_DEG          (360.0f / STEPS_PER_REV)

#define RPM_MIN           10.0f
#define RPM_MAX           100.0f
#define RPM_UNLOCK_MAX    400.0f
#define RPM_DEFAULT       20.0f
#define RPM_START         30.0f      /* rate the motor can start at instantly */
#define RAMP_RPM_PER_S    200.0f     /* acceleration limit */
#define FASTER_FACTOR     1.05f      /* ~5% per press */

#define MOVE_SETTLE_S     0.15       /* wait after a move before reading encoder */
#define MOVE_TOL_DEG      1.35f      /* correct again only if error > 0.75 step */
#define MOVE_MAX_ITER     4
#define HOLD_BAND_DEG     1.0f       /* feedback hold band (+/-) */

#define IR_PULSES_PER_REV 2
#define IR_DEBOUNCE_S     0.004
#define ENC_WINDOW        50         /* 50 samples @ 100 Hz = 0.5 s */
#define TELEMETRY_HZ      5
#define LOG_HZ            100
#define RFCOMM_CHANNEL    1
#define MAX_LINE          160

/* ------------------------------------------------------------------- state */
typedef enum { ACT_IDLE, ACT_RUN, ACT_MOVE, ACT_HOLD } activity_t;
typedef enum { SRC_RFCOMM, SRC_BLE } source_t;

static volatile sig_atomic_t run_program = 1;
static unsigned int timer_freq = 54000000U;
static int motor_pins[4] = { PIN_A, PIN_B, PIN_C, PIN_D };

/* Command/request state (state_lock) */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static int   req_run;                 /* continuous run requested */
static int   dir_cw = 1;              /* 1 = CW (assembly direction 1) */
static float desired_rpm = RPM_DEFAULT;
static int   maintain_mode;           /* 0 = Manual, 1 = Maintain */
static int   unlock_on;
static int   calibrated;
static int   hold_enabled;
static int   move_req;                /* new position move requested */
static float move_req_target;
static float move_req_rpm;
static unsigned int cmd_generation;   /* bumps on STOP/new command to abort moves */

/* Motor-thread outputs (state_lock) */
static activity_t activity = ACT_IDLE;
static float cur_cmd_rpm;             /* actual commanded step rate (ramped) */
static long  step_count;              /* signed steps since calibrate (angle sense) */
static int   coils_enabled;
static float cmd_angle = NAN;         /* active angle target */
static int   hold_corrections;

/* Encoder (enc_lock) */
static pthread_mutex_t enc_lock = PTHREAD_MUTEX_INITIALIZER;
static int   encoder_ok;
static int   enc_polarity = 1;        /* -i flips */
static float enc_raw_deg;             /* raw AS5600 angle */
static float enc_zero_raw;            /* raw angle at calibrate zero */
static float enc_angle;               /* polarity-corrected, zero-offset, 0..360 */
static double enc_cum;                /* unwrapped angle since calibrate */
static float enc_rpm;                 /* windowed, magnitude */
static int   enc_fresh;               /* samples since last reset */
static int   enc_have_sample;         /* at least one good reading */

/* Relation between assembly step direction and encoder angle (+1/-1, 0=unknown) */
static int   enc_sign;
/* Phase reference: coil index <-> raw encoder angle (for re-aligning coils) */
static int   phase_ref_valid;
static int   phase_ref_idx;
static float phase_ref_raw;

/* IR (ir_lock) */
static pthread_mutex_t ir_lock = PTHREAD_MUTEX_INITIALIZER;
static double ir_edges[3];
static int    ir_edge_n;
static float  ir_rpm_reported;

/* Logging (log_lock) */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE  *log_fp;
static char   log_name[160];
static char   log_dir[128] = "logs";
static double log_start;
static char   log_event[64];

/* Transports */
static pthread_mutex_t rf_lock = PTHREAD_MUTEX_INITIALIZER;
static int rf_client = -1;

/* ---------------------------------------------------------------- helpers */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static float wrap360(float a)
{
    a = fmodf(a, 360.0f);
    return a < 0 ? a + 360.0f : a;
}

static float wrap180(float a)
{
    a = wrap360(a);
    return a > 180.0f ? a - 360.0f : a;
}

static float rpm_limit(void) { return unlock_on ? RPM_UNLOCK_MAX : RPM_MAX; }

static void sleep_s(double s)
{
    struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
    nanosleep(&ts, NULL);
}

static void handle_signal(int sig) { (void)sig; run_program = 0; }

/* -------------------------------------------------------------- transports */
static void rf_send(const char *line)
{
    pthread_mutex_lock(&rf_lock);
    if (rf_client >= 0) {
        char buf[600];
        int len = snprintf(buf, sizeof(buf), "%s\n", line);
        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        for (int sent = 0; sent < len;) {
            ssize_t n = send(rf_client, buf + sent, (size_t)(len - sent), MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            sent += (int)n;
        }
    }
    pthread_mutex_unlock(&rf_lock);
}

static void reply(source_t src, const char *line)
{
    printf("-> %s\n", line);
    fflush(stdout);
    if (src == SRC_RFCOMM) rf_send(line);
    else ble_send_line(line);
}

static void broadcast(const char *line)
{
#ifdef PARMCO_SIM
    if (strncmp(line, "TEL:", 4)) printf("~> %s\n", line);
#endif
    rf_send(line);
    ble_send_line(line);
}

/* ------------------------------------------------------------ motor output */
static void coils_off(void)
{
    enable_routine(PIN_ENABLE, 0);
    for (int i = 0; i < 4; i++) E4235_Write(motor_pins[i], 0);
    pthread_mutex_lock(&state_lock);
    coils_enabled = 0;
    pthread_mutex_unlock(&state_lock);
}

/* Drive coil pattern 'idx' (no motion if the rotor is already there). */
static void apply_index(int idx)
{
    set_step_index(idx - 1);
    step_routine(1, STEP_MODE, motor_pins);
}

/* Energize the current pattern from standstill and record the phase reference. */
static void coils_on(void)
{
    apply_index(get_step_index());          /* pins first (EN low -> outputs off) */
    enable_routine(PIN_ENABLE, 1);
    pthread_mutex_lock(&state_lock);
    coils_enabled = 1;
    pthread_mutex_unlock(&state_lock);
    sleep_s(0.03);                           /* let the rotor settle on the pattern */
    pthread_mutex_lock(&enc_lock);
    if (encoder_ok) {
        phase_ref_idx = get_step_index();
        phase_ref_raw = enc_raw_deg;
        phase_ref_valid = 1;
    }
    pthread_mutex_unlock(&enc_lock);
}

/* Coil index whose equilibrium is closest to the rotor's present angle. */
static int index_for_current_angle(void)
{
    int idx = get_step_index();
    pthread_mutex_lock(&enc_lock);
    if (phase_ref_valid && enc_sign != 0) {
        float d = wrap180(enc_polarity * (enc_raw_deg - phase_ref_raw));
        idx = phase_ref_idx + enc_sign * (int)lroundf(d / STEP_DEG);
    }
    pthread_mutex_unlock(&enc_lock);
    return idx;
}

static float read_angle(void)
{
    pthread_mutex_lock(&enc_lock);
    float a = enc_angle;
    pthread_mutex_unlock(&enc_lock);
    return a;
}

static double read_cum(void)
{
    pthread_mutex_lock(&enc_lock);
    double c = enc_cum;
    pthread_mutex_unlock(&enc_lock);
    return c;
}

/* ------------------------------------------------------------ encoder sign */
static long   learn_steps;
static double learn_cum0;

static void learn_reset(void)
{
    learn_steps = 0;
    learn_cum0 = read_cum();
}

/* Called after every step with asm direction; learns enc_sign once. */
static void learn_update(int asm_dir)
{
    if (enc_sign != 0 || !encoder_ok) return;
    learn_steps += asm_dir ? 1 : -1;
    if (labs(learn_steps) >= 20) {
        double d = read_cum() - learn_cum0;
        double expect = fabs((double)learn_steps) * STEP_DEG;
        if (fabs(d) > 0.5 * expect) {
            int s = ((d > 0) == (learn_steps > 0)) ? 1 : -1;
            pthread_mutex_lock(&state_lock);
            enc_sign = s;
            if (s < 0) step_count = -step_count;
            pthread_mutex_unlock(&state_lock);
            printf("Learned encoder direction: asm dir 1 -> angle %s\n", s > 0 ? "increases" : "decreases");
            fflush(stdout);
        }
        learn_reset();
    }
}

/* One step at the given rate, timed by the assembly step-timing loop. */
static unsigned int next_deadline;

static void timed_step(int asm_dir, float rpm)
{
    if (rpm < 1.0f) rpm = 1.0f;
    unsigned int period = (unsigned int)((double)timer_freq * 60.0 / (rpm * STEPS_PER_REV));
    unsigned int now = get_current_timer();
    int late = (int)(now - next_deadline);
    if (late > (int)period) next_deadline = now;        /* recover after a hiccup */
    /* Sleep until ~200 us before the deadline, then let the assembly loop
       busy-wait the rest. Spinning only briefly leaves the CPU free for the
       kernel's Bluetooth work (long spins at RT priority dropped BLE links). */
    int wait = (int)(next_deadline - now);
    int spin = (int)(timer_freq / 5000);                /* 200 us in ticks */
    if (wait > spin) {
        double sleep_ns = (double)(wait - spin) / timer_freq * 1e9;
        struct timespec ts = { (time_t)(sleep_ns / 1e9), (long)fmod(sleep_ns, 1e9) };
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }
    step_at_deadline(next_deadline, asm_dir, STEP_MODE, motor_pins);
    next_deadline += period;

    pthread_mutex_lock(&state_lock);
    step_count += (asm_dir ? 1 : -1) * (enc_sign ? enc_sign : 1);
    cur_cmd_rpm = rpm;
    pthread_mutex_unlock(&state_lock);
    learn_update(asm_dir);
}

/* Accelerate toward target by the ramp limit for one step period. */
static float ramp_toward(float cur, float target)
{
    float dt = 60.0f / (cur * STEPS_PER_REV);
    float dv = RAMP_RPM_PER_S * dt;
    if (cur < target) return fminf(cur + dv, target);
    if (cur > target) return fmaxf(cur - dv, target);
    return cur;
}

/* ---------------------------------------------------------------- moves */
typedef enum { MV_PLAN, MV_STEP, MV_SETTLE } move_phase_t;
static move_phase_t mv_phase = MV_PLAN;

static float  mv_target, mv_rpm, mv_cur;
static int    mv_dir, mv_iter, mv_probe, mv_from_hold;
static long   mv_left;
static double mv_settle_until, mv_t0;
static float  mv_probe_angle;
static float  mv_angle_before;
static long   mv_last_n;
static int    mv_last_dir, mv_bad, mv_probe_tries;
static unsigned int mv_gen;

static void notify_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notify_event(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    broadcast(buf);
    printf("%s\n", buf);
    fflush(stdout);
}

static void start_move(float target, float rpm, int from_hold)
{
    mv_target = wrap360(target);
    mv_rpm = fminf(fmaxf(rpm, RPM_MIN), RPM_MAX);
    mv_iter = 0;
    mv_probe = 0;
    mv_from_hold = from_hold;
    mv_t0 = now_s();
    mv_phase = MV_PLAN;
    mv_last_n = 0;
    mv_bad = 0;
    mv_probe_tries = 0;
    pthread_mutex_lock(&state_lock);
    activity = ACT_MOVE;
    mv_gen = cmd_generation;
    if (!from_hold) cmd_angle = mv_target;
    pthread_mutex_unlock(&state_lock);
}

/* Returns 1 while the move is still in progress. */
static int move_service(void)
{
    switch (mv_phase) {
    case MV_PLAN: {
        if (!coils_enabled) { coils_on(); next_deadline = get_current_timer(); }
        if (enc_sign == 0) {                       /* learn direction with a short probe */
            mv_probe = 1;
            mv_probe_angle = read_angle();
            mv_dir = 1;
            mv_left = 10;
            mv_cur = mv_probe_tries ? 12.0f : (RPM_START < mv_rpm ? RPM_START : mv_rpm);
            mv_probe_tries++;
            mv_phase = MV_STEP;
            return 1;
        }
        float now_angle = read_angle();
        float err = wrap180(mv_target - now_angle);
        if (mv_last_n > 0) {
            float expect = (mv_last_dir ? 1.0f : -1.0f) * enc_sign * mv_last_n * STEP_DEG;
            float moved = wrap180(now_angle - mv_angle_before);
            printf("MOVE check: commanded %+.1f deg, encoder moved %+.1f deg\n", expect, moved);
            if (fabsf(expect) >= 5.0f && fabsf(moved) > 0.4f * fabsf(expect) &&
                ((moved > 0) != (expect > 0))) {
                pthread_mutex_lock(&state_lock);
                enc_sign = -enc_sign;
                step_count = -step_count;
                pthread_mutex_unlock(&state_lock);
                printf("MOVE: encoder moved opposite to the command -> direction corrected (sign %+d)\n", enc_sign);
            } else if (fabsf(expect) >= 5.0f && fabsf(moved) < 0.25f * fabsf(expect)) {
                mv_bad++;
                printf("MOVE: encoder barely moved (motor stalled or encoder not reading)\n");
            }
        }
        long n = lroundf(fabsf(err) / STEP_DEG);
        int failed = (mv_bad >= 2) || (mv_iter >= MOVE_MAX_ITER && fabsf(err) > MOVE_TOL_DEG);
        if (n == 0 || (mv_iter > 0 && fabsf(err) <= MOVE_TOL_DEG) || mv_iter >= MOVE_MAX_ITER || failed) {
            /* done */
            pthread_mutex_lock(&enc_lock);
            phase_ref_idx = get_step_index();
            phase_ref_raw = enc_raw_deg;
            phase_ref_valid = encoder_ok;
            pthread_mutex_unlock(&enc_lock);
            double dur = now_s() - mv_t0;
            if (mv_from_hold) {
                pthread_mutex_lock(&state_lock);
                hold_corrections++;
                int nc = hold_corrections;
                pthread_mutex_unlock(&state_lock);
                notify_event("EVT:HOLD_CORRECTION:n=%d;err=%.2f;ms=%.0f", nc, err, dur * 1000);
                pthread_mutex_lock(&log_lock);
                snprintf(log_event, sizeof(log_event), "hold_correction_%d", nc);
                pthread_mutex_unlock(&log_lock);
            } else {
                notify_event("%s:target=%.2f;angle=%.2f;err=%.2f;ms=%.0f",
                             failed ? "EVT:MOVE_FAILED" : "EVT:MOVE_DONE",
                             mv_target, read_angle(), err, dur * 1000);
            }
            int hold;
            pthread_mutex_lock(&state_lock);
            hold = hold_enabled;
            pthread_mutex_unlock(&state_lock);
            if (hold) {
                coils_off();
                pthread_mutex_lock(&state_lock);
                activity = ACT_HOLD;
                pthread_mutex_unlock(&state_lock);
            } else {
                pthread_mutex_lock(&state_lock);
                activity = ACT_IDLE;                  /* coils stay energized */
                pthread_mutex_unlock(&state_lock);
            }
            mv_phase = MV_PLAN;
            return 0;
        }
        mv_left = n;
        mv_dir = (err * enc_sign > 0) ? 1 : 0;
        mv_angle_before = now_angle;
        mv_last_n = n;
        mv_last_dir = mv_dir;
        printf("MOVE iter %d: angle %.2f err %+.2f -> %ld steps asm_dir %d (sign %+d)\n",
               mv_iter, now_angle, err, n, mv_dir, enc_sign);
        mv_cur = RPM_START < mv_rpm ? RPM_START : mv_rpm;
        next_deadline = get_current_timer();
        mv_phase = MV_STEP;
        return 1;
    }
    case MV_STEP: {
        /* accelerate, and decelerate so we can stop at RPM_START by the end */
        float vmax_rps = sqrtf((RPM_START / 60.0f) * (RPM_START / 60.0f) +
                               2.0f * (RAMP_RPM_PER_S / 60.0f) * ((float)mv_left / STEPS_PER_REV));
        float target = fminf(mv_rpm, vmax_rps * 60.0f);
        mv_cur = ramp_toward(mv_cur, target);
        if (mv_cur > target) mv_cur = target;
        timed_step(mv_dir, mv_cur);
        if (--mv_left <= 0) {
            mv_settle_until = now_s() + MOVE_SETTLE_S;
            mv_phase = MV_SETTLE;
        }
        return 1;
    }
    case MV_SETTLE:
        if (now_s() < mv_settle_until) { usleep(2000); return 1; }
        if (mv_probe) {
            float d = wrap180(read_angle() - mv_probe_angle);
            printf("MOVE probe: 10 steps asm_dir 1 -> encoder moved %+.2f deg\n", d);
            if (fabsf(d) >= 7.0f) {
                pthread_mutex_lock(&state_lock);
                enc_sign = (d > 0) ? 1 : -1;
                if (enc_sign < 0) step_count = -step_count;
                pthread_mutex_unlock(&state_lock);
                printf("Learned encoder direction: asm dir 1 -> angle %s\n",
                       enc_sign > 0 ? "increases" : "decreases");
            } else if (mv_probe_tries >= 2) {
                mv_probe = 0;
                notify_event("EVT:MOVE_FAILED:encoder_not_tracking;moved=%.2f", d);
                pthread_mutex_lock(&state_lock);
                activity = ACT_IDLE;
                pthread_mutex_unlock(&state_lock);
                mv_phase = MV_PLAN;
                return 0;
            }
            mv_probe = 0;                              /* PLAN probes again if needed */
        } else {
            mv_iter++;                                 /* PLAN re-checks the error */
        }
        mv_phase = MV_PLAN;
        return 1;
    }
    return 1;
}

/* --------------------------------------------------------------- maintain */
static double mt_last, mt_low_since;
static float  mt_integral;

static float maintain_target(float desired, float cur)
{
    double t = now_s();
    if (t - mt_last < 0.1) return desired + mt_integral;
    mt_last = t;
    pthread_mutex_lock(&enc_lock);
    float meas = encoder_ok ? enc_rpm : NAN;
    pthread_mutex_unlock(&enc_lock);
    if (isnan(meas)) {
        pthread_mutex_lock(&ir_lock);
        meas = ir_rpm_reported;
        pthread_mutex_unlock(&ir_lock);
    }
    if (fabsf(cur - desired) > 1.0f) return desired + mt_integral;   /* still ramping */

    /* Lost sync (stalled/pushed): re-align coils to the rotor and restart ramp. */
    if (meas < 0.5f * desired) {
        if (mt_low_since == 0) mt_low_since = t;
        if (t - mt_low_since > 0.3) {
            apply_index(index_for_current_angle());
            mt_low_since = 0;
            mt_integral = 0;
            return -1.0f;
        }
    } else {
        mt_low_since = 0;
    }
    float err = desired - meas;
    mt_integral += 0.5f * err * 0.1f;
    float lim = 0.2f * desired;
    if (mt_integral > lim) mt_integral = lim;
    if (mt_integral < -lim) mt_integral = -lim;
    return desired + 0.3f * err + mt_integral;
}

/* ------------------------------------------------------------ motor thread */
static void *motor_thread(void *arg)
{
    (void)arg;
    /* Run on core 3 (core 0 services most interrupts, incl. Bluetooth UART). */
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(3, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);
    struct sched_param sp = { .sched_priority = 40 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        printf("Note: real-time priority unavailable (run with sudo)\n");

    float run_rpm = 0;
    double hold_check = 0;
    int last_dir = 1;

    while (run_program) {
        pthread_mutex_lock(&state_lock);
        int want_run = req_run;
        int dir = dir_cw;
        float desired = desired_rpm;
        int mm = maintain_mode;
        int mreq = move_req;
        float mtarget = move_req_target, mrpm = move_req_rpm;
        move_req = 0;
        activity_t act = activity;
        unsigned int gen = cmd_generation;
        int hold = hold_enabled;
        float htarget = cmd_angle;
        pthread_mutex_unlock(&state_lock);

        if (mreq) {
            start_move(mtarget, mrpm, 0);
            continue;
        }

        if (want_run) {
            if (act != ACT_RUN) {
                if (!coils_enabled) coils_on();
                run_rpm = fminf(RPM_START, desired);
                last_dir = dir;
                next_deadline = get_current_timer();
                mt_integral = 0; mt_low_since = 0;
                learn_reset();
                pthread_mutex_lock(&state_lock);
                activity = ACT_RUN;
                cmd_angle = NAN;
                pthread_mutex_unlock(&state_lock);
            }
            if (dir != last_dir) {                    /* reversing: restart the ramp */
                run_rpm = fminf(RPM_START, desired);
                last_dir = dir;
            }
            float target = desired;
            if (mm) {
                target = maintain_target(desired, run_rpm);
                if (target < 0) { run_rpm = RPM_START < desired ? RPM_START : desired; target = desired; }
            }
            run_rpm = ramp_toward(run_rpm, target);
            timed_step(dir ? 1 : 0, run_rpm);
            continue;
        }

        if (act == ACT_RUN) {                         /* stop requested */
            coils_off();
            pthread_mutex_lock(&state_lock);
            activity = ACT_IDLE;
            cur_cmd_rpm = 0;
            pthread_mutex_unlock(&state_lock);
            continue;
        }

        if (act == ACT_MOVE) {
            if (gen != mv_gen) {                      /* aborted by STOP */
                coils_off();
                pthread_mutex_lock(&state_lock);
                activity = ACT_IDLE;
                cur_cmd_rpm = 0;
                pthread_mutex_unlock(&state_lock);
                continue;
            }
            if (!move_service()) {
                pthread_mutex_lock(&state_lock);
                cur_cmd_rpm = 0;
                pthread_mutex_unlock(&state_lock);
            }
            continue;
        }

        if (act == ACT_HOLD) {
            if (!hold) {                              /* hold switched off */
                pthread_mutex_lock(&state_lock);
                activity = ACT_IDLE;
                pthread_mutex_unlock(&state_lock);
                continue;
            }
            double t = now_s();
            if (t >= hold_check) {
                hold_check = t + 0.01;
                float reach = wrap360(lroundf(htarget / STEP_DEG) * STEP_DEG);
                float err = wrap180(read_angle() - reach);
                if (fabsf(err) > HOLD_BAND_DEG && enc_sign != 0) {
                    /* re-align the coil pattern with where the rotor is now,
                       then step back to the target (spec: feedback hold item 4) */
                    apply_index(index_for_current_angle());
                    enable_routine(PIN_ENABLE, 1);
                    pthread_mutex_lock(&state_lock);
                    coils_enabled = 1;
                    pthread_mutex_unlock(&state_lock);
                    start_move(reach, RPM_START, 1);
                    continue;
                }
            }
            usleep(2000);
            continue;
        }

        usleep(2000);                                 /* idle */
    }
    coils_off();
    return NULL;
}

/* ---------------------------------------------------------- encoder thread */
static void *encoder_thread(void *arg)
{
    (void)arg;
    double t_hist[ENC_WINDOW];
    double c_hist[ENC_WINDOW];
    int head = 0, count = 0;
    float prev_angle = 0;
    int have_prev = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (run_program) {
        next.tv_nsec += 10000000L;
        if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (!encoder_ok) continue;

        float raw;
        static int fails;
        static double fail_report;
        if (as5600_read_degrees(&raw) != 0) {
            fails++;
            double tf = now_s();
            if (tf - fail_report > 1.0) {
                printf("[ENC] %d AS5600 read errors in the last second\n", fails);
                fails = 0;
                fail_report = tf;
            }
            continue;
        }
        double t = now_s();

        pthread_mutex_lock(&enc_lock);
        enc_raw_deg = raw;
        enc_have_sample = 1;
        float ang = wrap360(enc_polarity * (raw - enc_zero_raw));
        if (have_prev && enc_fresh) enc_cum += wrap180(ang - prev_angle);
        prev_angle = ang;
        have_prev = 1;
        if (!enc_fresh) { count = 0; head = 0; }
        enc_fresh = 1;
        enc_angle = ang;

        t_hist[head] = t;
        c_hist[head] = enc_cum;
        int oldest = (head + 1) % ENC_WINDOW;
        head = oldest;
        if (count < ENC_WINDOW) count++;
        if (count >= 10) {
            int o = (count < ENC_WINDOW) ? 0 : oldest;
            int newest = (head - 1 + ENC_WINDOW) % ENC_WINDOW;
            double dt = t_hist[newest] - t_hist[o];
            if (dt > 0) enc_rpm = (float)(fabs(c_hist[newest] - c_hist[o]) / dt / 6.0);
        }
        pthread_mutex_unlock(&enc_lock);
    }
    return NULL;
}

/* --------------------------------------------------------------- IR thread */
static void *ir_thread(void *arg)
{
    (void)arg;
    int last = 1;
    while (run_program) {
        int cur = E4235_Read(PIN_IR);           /* class library GPIO read */
        if (cur >= 0) {
            if (last == 1 && cur == 0) {         /* beam broken: falling edge */
                double t = now_s();
                pthread_mutex_lock(&ir_lock);
                if (ir_edge_n == 0 || t - ir_edges[0] > IR_DEBOUNCE_S) {
                    ir_edges[2] = ir_edges[1];
                    ir_edges[1] = ir_edges[0];
                    ir_edges[0] = t;
                    if (ir_edge_n < 3) ir_edge_n++;
                }
                pthread_mutex_unlock(&ir_lock);
            }
            last = cur;
        }
        usleep(500);                             /* ~2 kHz polling */
    }
    return NULL;
}

/* RPM from the period between the most recent blade pulses. */
static float ir_rpm_now(void)
{
    float rpm = 0;
    double t = now_s();
    pthread_mutex_lock(&ir_lock);
    if (ir_edge_n >= 2) {
        double period = (ir_edge_n >= 3) ? (ir_edges[0] - ir_edges[2]) / 2.0
                                         : (ir_edges[0] - ir_edges[1]);
        double timeout = fmax(2.5 * period, 2.0);
        if (period > 0 && t - ir_edges[0] < timeout)
            rpm = (float)(60.0 / (period * IR_PULSES_PER_REV));
        else
            ir_edge_n = 0;                       /* stale: stopped */
    }
    ir_rpm_reported = rpm;
    pthread_mutex_unlock(&ir_lock);
    return rpm;
}

/* ----------------------------------------------------------------- logging */
static const char *mode_name(activity_t act, int mm, int unl)
{
    if (act == ACT_MOVE || act == ACT_HOLD) return "position";
    if (mm) return "Maintain";
    if (unl) return "open-loop";
    return "Manual";
}

static int log_start_file(const char *label, char *out, size_t outlen)
{
    pthread_mutex_lock(&log_lock);
    if (log_fp) { pthread_mutex_unlock(&log_lock); return -2; }
    mkdir(log_dir, 0755);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char stamp[40];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
    if (label && *label)
        snprintf(log_name, sizeof(log_name), "log_%s_%s.csv", stamp, label);
    else
        snprintf(log_name, sizeof(log_name), "log_%s.csv", stamp);
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", log_dir, log_name);
    log_fp = fopen(path, "w");
    if (!log_fp) { pthread_mutex_unlock(&log_lock); return -1; }
    setvbuf(log_fp, NULL, _IOFBF, 1 << 16);
    fprintf(log_fp, "time_s,mode,cmd_rpm,cmd_angle_deg,step_count,step_angle_deg,"
                    "enc_angle_deg,enc_cum_deg,ir_rpm,enc_rpm,app_rpm,enable,event\n");
    log_start = now_s();
    log_event[0] = '\0';
    snprintf(out, outlen, "%s", log_name);
    pthread_mutex_unlock(&log_lock);
    return 0;
}

static void log_stop_file(char *out, size_t outlen)
{
    pthread_mutex_lock(&log_lock);
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
        snprintf(out, outlen, "%s", log_name);
    } else {
        out[0] = '\0';
    }
    pthread_mutex_unlock(&log_lock);
}

static void *log_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (run_program) {
        next.tv_nsec += 1000000000L / LOG_HZ;
        if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        pthread_mutex_lock(&log_lock);
        if (!log_fp) { pthread_mutex_unlock(&log_lock); continue; }
        pthread_mutex_unlock(&log_lock);

        pthread_mutex_lock(&state_lock);
        activity_t act = activity;
        int mm = maintain_mode, unl = unlock_on, en = coils_enabled;
        float crpm = (act == ACT_RUN || act == ACT_MOVE) ? cur_cmd_rpm : 0.0f;
        float cang = cmd_angle;
        long sc = step_count;
        pthread_mutex_unlock(&state_lock);

        pthread_mutex_lock(&enc_lock);
        float ea = enc_angle, er = enc_rpm;
        double ec = enc_cum;
        int eok = encoder_ok;
        pthread_mutex_unlock(&enc_lock);

        float ir = ir_rpm_now();

        pthread_mutex_lock(&log_lock);
        if (log_fp) {
            char angbuf[16] = "", eabuf[16] = "nan", ecbuf[20] = "nan", erbuf[16] = "nan";
            if (!isnan(cang) && act != ACT_RUN)
                snprintf(angbuf, sizeof(angbuf), "%.2f", cang);
            if (eok) {
                snprintf(eabuf, sizeof(eabuf), "%.2f", ea);
                snprintf(ecbuf, sizeof(ecbuf), "%.2f", ec);
                snprintf(erbuf, sizeof(erbuf), "%.2f", er);
            }
            fprintf(log_fp, "%.4f,%s,%.2f,%s,%ld,%.2f,%s,%s,%.2f,%s,%.2f,%d,%s\n",
                    now_s(), mode_name(act, mm, unl), crpm, angbuf, sc, sc * STEP_DEG,
                    eabuf, ecbuf, ir, erbuf, ir, en, log_event);
            log_event[0] = '\0';
        }
        pthread_mutex_unlock(&log_lock);
    }
    return NULL;
}

/* --------------------------------------------------------------- telemetry */
static void build_telemetry(char *buf, size_t n)
{
    pthread_mutex_lock(&state_lock);
    activity_t act = activity;
    int run = (act == ACT_RUN), cal = calibrated, d = dir_cw, mm = maintain_mode;
    int unl = unlock_on, en = coils_enabled, hold = hold_enabled;
    float des = desired_rpm;
    pthread_mutex_unlock(&state_lock);

    pthread_mutex_lock(&enc_lock);
    float ea = enc_angle, er = enc_rpm;
    int eok = encoder_ok;
    pthread_mutex_unlock(&enc_lock);

    pthread_mutex_lock(&log_lock);
    int logging = log_fp != NULL;
    double lt = logging ? now_s() - log_start : 0;
    char lname[160];
    snprintf(lname, sizeof(lname), "%s", logging ? log_name : "");
    pthread_mutex_unlock(&log_lock);

    static const char *acts[] = { "IDLE", "RUN", "MOVE", "HOLD" };
    snprintf(buf, n,
             "TEL:ir=%.1f;enc=%.1f;ang=%.1f;eok=%d;cal=%d;run=%d;dir=%s;rpm=%.1f;mode=%s;"
             "unlock=%d;act=%s;en=%d;hold=%d;log=%d;logt=%.0f;file=%s",
             ir_rpm_now(), eok ? er : 0.0f, eok ? ea : 0.0f, eok, cal, run, d ? "CW" : "CCW",
             des, mm ? "MAINTAIN" : "MANUAL", unl, acts[act], en, hold, logging, lt, lname);
}

static void *telemetry_thread(void *arg)
{
    (void)arg;
    int tick = 0;
    while (run_program) {
        sleep_s(1.0 / TELEMETRY_HZ);
        char tel[400];
        build_telemetry(tel, sizeof(tel));
        broadcast(tel);
        /* Legacy lines for the Android app, once per second */
        if (++tick % TELEMETRY_HZ == 0) {
            char l[64];
            pthread_mutex_lock(&enc_lock);
            float ea = enc_angle, er = enc_rpm;
            int eok = encoder_ok;
            pthread_mutex_unlock(&enc_lock);
            snprintf(l, sizeof(l), "RPM_ACTUAL:%.2f", ir_rpm_reported);
            rf_send(l);
            if (eok) {
                snprintf(l, sizeof(l), "ENCODER_RPM:%.2f", er);
                rf_send(l);
                snprintf(l, sizeof(l), "ANGLE_ACTUAL:%.2f", ea);
                rf_send(l);
            }
        }
    }
    return NULL;
}

/* --------------------------------------------------------- command handling */
static void apply_defaults(void)
{
    pthread_mutex_lock(&state_lock);
    req_run = 0;
    move_req = 0;
    cmd_generation++;
    dir_cw = 1;
    desired_rpm = RPM_DEFAULT;
    maintain_mode = 0;
    unlock_on = 0;
    calibrated = 0;
    cmd_angle = NAN;
    if (activity == ACT_HOLD) activity = ACT_IDLE;
    pthread_mutex_unlock(&state_lock);
    if (activity == ACT_IDLE && coils_enabled) coils_off();
}

static void stop_all(void)
{
    pthread_mutex_lock(&state_lock);
    req_run = 0;
    move_req = 0;
    cmd_generation++;
    if (unlock_on) {
        unlock_on = 0;                        /* unlock switches off on stop */
        if (desired_rpm > RPM_MAX) desired_rpm = RPM_MAX;
    }
    if (activity == ACT_HOLD) activity = ACT_IDLE;
    pthread_mutex_unlock(&state_lock);
    /* motor thread turns coils off; also cover the idle-energized case */
    if (activity == ACT_IDLE) coils_off();
}

static void process_line(source_t src, const char *raw)
{
    char cmd[MAX_LINE];
    char resp[256];
    snprintf(cmd, sizeof(cmd), "%s", raw);
    /* trim */
    char *s = cmd;
    while (*s == ' ') s++;
    for (char *e = s + strlen(s) - 1; e >= s && (*e == ' ' || *e == '\r'); e--) *e = '\0';
    printf("<- %s\n", s);
    fflush(stdout);

    if (!strcmp(s, "HELLO")) {
        reply(src, "PARMCO:CONNECTED");
        build_telemetry(resp, sizeof(resp));
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "STATUS")) {
        build_telemetry(resp, sizeof(resp));
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "START")) {
        pthread_mutex_lock(&state_lock);
        req_run = 1;
        cmd_generation++;
        pthread_mutex_unlock(&state_lock);
        reply(src, "ACK:START");
        return;
    }
    if (!strcmp(s, "STOP")) {
        stop_all();
        reply(src, "ACK:STOP");
        return;
    }
    if (!strcmp(s, "DIR:CW") || !strcmp(s, "DIR:CCW")) {
        pthread_mutex_lock(&state_lock);
        dir_cw = !strcmp(s, "DIR:CW");
        pthread_mutex_unlock(&state_lock);
        snprintf(resp, sizeof(resp), "ACK:%s", s);
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "FASTER") || !strcmp(s, "SLOWER")) {
        pthread_mutex_lock(&state_lock);
        float r = !strcmp(s, "FASTER") ? desired_rpm * FASTER_FACTOR : desired_rpm / FASTER_FACTOR;
        if (r > rpm_limit()) r = rpm_limit();
        if (r < RPM_MIN) r = RPM_MIN;
        desired_rpm = r;
        pthread_mutex_unlock(&state_lock);
        snprintf(resp, sizeof(resp), "ACK:%s:%.1f", s, r);
        reply(src, resp);
        return;
    }
    if (!strncmp(s, "RPM:", 4)) {
        char *end = NULL;
        errno = 0;
        float r = strtof(s + 4, &end);
        pthread_mutex_lock(&state_lock);
        float lim = rpm_limit();
        int ok = !errno && end != s + 4 && *end == '\0' && r >= RPM_MIN && r <= lim;
        if (ok) desired_rpm = r;
        pthread_mutex_unlock(&state_lock);
        if (ok) snprintf(resp, sizeof(resp), "ACK:RPM:%.1f", r);
        else    snprintf(resp, sizeof(resp), "ERR:INVALID_RPM:%s", s + 4);
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "MODE:MANUAL") || !strcmp(s, "MAINTAIN:OFF") ||
        !strcmp(s, "MODE:MAINTAIN") || !strcmp(s, "MAINTAIN:ON")) {
        int m = !strcmp(s, "MODE:MAINTAIN") || !strcmp(s, "MAINTAIN:ON");
        pthread_mutex_lock(&state_lock);
        maintain_mode = m;
        pthread_mutex_unlock(&state_lock);
        reply(src, m ? "ACK:MODE:MAINTAIN" : "ACK:MODE:MANUAL");
        return;
    }
    if (!strcmp(s, "UNLOCK:ON") || !strcmp(s, "UNLOCK:OFF")) {
        int on = !strcmp(s, "UNLOCK:ON");
        pthread_mutex_lock(&state_lock);
        unlock_on = on;
        if (!on && desired_rpm > RPM_MAX) desired_rpm = RPM_MAX;
        pthread_mutex_unlock(&state_lock);
        reply(src, on ? "ACK:UNLOCK:ON" : "ACK:UNLOCK:OFF");
        return;
    }
    if (!strcmp(s, "CALIBRATE_ZERO")) {
        if (!encoder_ok) { reply(src, "ERR:ENCODER_UNAVAILABLE"); return; }
        if (!enc_have_sample) { reply(src, "ERR:ENCODER_NOT_READY"); return; }
        pthread_mutex_lock(&state_lock);
        int busy = req_run || activity == ACT_MOVE;
        pthread_mutex_unlock(&state_lock);
        if (busy) { reply(src, "ERR:STOP_FIRST"); return; }
        pthread_mutex_lock(&enc_lock);
        enc_zero_raw = enc_raw_deg;
        enc_angle = 0;
        enc_cum = 0;
        enc_fresh = 0;
        pthread_mutex_unlock(&enc_lock);
        pthread_mutex_lock(&state_lock);
        calibrated = 1;
        step_count = 0;
        cmd_angle = NAN;
        if (activity == ACT_HOLD) activity = ACT_IDLE;
        pthread_mutex_unlock(&state_lock);
        reply(src, "ACK:CALIBRATE_ZERO");
        return;
    }
    if (!strcmp(s, "SET_ZERO") || !strncmp(s, "ANGLE:", 6)) {
        float target = 0;
        if (s[0] == 'A') {
            char *end = NULL;
            errno = 0;
            target = strtof(s + 6, &end);
            if (errno || end == s + 6 || *end != '\0' || target < -360 || target > 720) {
                snprintf(resp, sizeof(resp), "ERR:INVALID_ANGLE:%s", s + 6);
                reply(src, resp);
                return;
            }
        }
        if (!encoder_ok) { reply(src, "ERR:ENCODER_UNAVAILABLE"); return; }
        pthread_mutex_lock(&state_lock);
        int cal = calibrated;
        if (cal) {
            req_run = 0;
            cmd_generation++;
            move_req = 1;
            move_req_target = wrap360(target);
            move_req_rpm = fminf(desired_rpm, RPM_MAX);
        }
        pthread_mutex_unlock(&state_lock);
        if (!cal) { reply(src, "ERR:NOT_CALIBRATED"); return; }
        if (s[0] == 'S') reply(src, "ACK:SET_ZERO");
        else { snprintf(resp, sizeof(resp), "ACK:ANGLE:%.2f", wrap360(target)); reply(src, resp); }
        return;
    }
    if (!strncmp(s, "LOG:START", 9)) {
        const char *label = (s[9] == ':') ? s + 10 : "";
        char name[160];
        int r = log_start_file(label, name, sizeof(name));
        if (r == 0) snprintf(resp, sizeof(resp), "LOG:STARTED:%s", name);
        else if (r == -2) snprintf(resp, sizeof(resp), "ERR:LOG_ALREADY_RUNNING");
        else snprintf(resp, sizeof(resp), "ERR:LOG_OPEN_FAILED");
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "LOG:STOP")) {
        char name[160];
        log_stop_file(name, sizeof(name));
        if (name[0]) snprintf(resp, sizeof(resp), "LOG:STOPPED:%s", name);
        else snprintf(resp, sizeof(resp), "ERR:LOG_NOT_RUNNING");
        reply(src, resp);
        return;
    }
    if (!strncmp(s, "MARK", 4)) {
        pthread_mutex_lock(&log_lock);
        snprintf(log_event, sizeof(log_event), "%s", s[4] == ':' ? s + 5 : "mark");
        for (char *p = log_event; *p; p++) if (*p == ',') *p = ' ';
        pthread_mutex_unlock(&log_lock);
        reply(src, "ACK:MARK");
        return;
    }
    if (!strcmp(s, "HOLD:ON") || !strcmp(s, "HOLD:OFF")) {
        pthread_mutex_lock(&state_lock);
        hold_enabled = !strcmp(s, "HOLD:ON");
        pthread_mutex_unlock(&state_lock);
        snprintf(resp, sizeof(resp), "ACK:%s", s);
        reply(src, resp);
        return;
    }
    if (!strcmp(s, "ENC:INVERT")) {
        pthread_mutex_lock(&enc_lock);
        enc_polarity = -enc_polarity;
        enc_fresh = 0;
        pthread_mutex_unlock(&enc_lock);
        pthread_mutex_lock(&state_lock);
        calibrated = 0;
        enc_sign = 0;
        pthread_mutex_unlock(&state_lock);
        reply(src, "ACK:ENC:INVERT (recalibrate)");
        return;
    }
    reply(src, "ERR:UNKNOWN_COMMAND");
}

/* ------------------------------------------------------- BLE callbacks */
static void ble_line(const char *line) { process_line(SRC_BLE, line); }

static void ble_conn(int connected)
{
    if (connected) {
        apply_defaults();
        printf("BLE phone connected: defaults applied\n");
    } else {
        stop_all();
        printf("BLE phone disconnected: motor stopped\n");
    }
    fflush(stdout);
}

/* ------------------------------------------------------ RFCOMM (Android) */
static void rfcomm_client(int fd)
{
    char line[MAX_LINE], buf[256];
    size_t pos = 0;
    int overflow = 0;
    while (run_program) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { 0, 200000 };
        int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0) break;
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (!overflow && pos > 0) { line[pos] = '\0'; process_line(SRC_RFCOMM, line); }
                pos = 0;
                overflow = 0;
            } else if (c != '\r' && !overflow) {
                if (pos < sizeof(line) - 1) line[pos++] = c;
                else overflow = 1;
            }
        }
    }
}

static void *rfcomm_thread(void *arg)
{
    (void)arg;
    int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (fd < 0) { perror("RFCOMM socket"); return NULL; }
    struct sockaddr_rc local = { 0 };
    local.rc_family = AF_BLUETOOTH;
    bacpy(&local.rc_bdaddr, BDADDR_ANY);
    local.rc_channel = RFCOMM_CHANNEL;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0 || listen(fd, 1) < 0) {
        perror("RFCOMM bind/listen");
        close(fd);
        return NULL;
    }
    printf("RFCOMM (Android) listening on channel %d\n", RFCOMM_CHANNEL);
    while (run_program) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { 0, 200000 };
        int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) continue;
        struct sockaddr_rc remote = { 0 };
        socklen_t len = sizeof(remote);
        int c = accept(fd, (struct sockaddr *)&remote, &len);
        if (c < 0) continue;
        char addr[18];
        ba2str(&remote.rc_bdaddr, addr);
        printf("Android connected: %s\n", addr);
        apply_defaults();
        pthread_mutex_lock(&rf_lock);
        rf_client = c;
        pthread_mutex_unlock(&rf_lock);
        rf_send("PARMCO:CONNECTED");
        rfcomm_client(c);
        pthread_mutex_lock(&rf_lock);
        rf_client = -1;
        pthread_mutex_unlock(&rf_lock);
        close(c);
        stop_all();
        printf("Android disconnected; motor stopped.\n");
    }
    close(fd);
    return NULL;
}

/* -------------------------------------------------------------------- main */
int main(int argc, char **argv)
{
    int secure = 0, opt;
    while ((opt = getopt(argc, argv, "iHsl:")) != -1) {
        switch (opt) {
        case 'i': enc_polarity = -1; break;
        case 'H': hold_enabled = 1; break;
        case 's': secure = 1; break;
        case 'l': snprintf(log_dir, sizeof(log_dir), "%s", optarg); break;
        default:
            fprintf(stderr, "usage: %s [-i] [-H] [-s] [-l logdir]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (E4235_Init() != 0) { fprintf(stderr, "E4235_Init failed\n"); return EXIT_FAILURE; }

    /* Quiet start: enable low and driver inputs low before anything else. */
    E4235_Select(PIN_ENABLE, 1);
    enable_routine(PIN_ENABLE, 0);
    for (int i = 0; i < 4; i++) { E4235_Select(motor_pins[i], 1); E4235_Write(motor_pins[i], 0); }
    E4235_Select(PIN_IR, 0);
    set_step_index(0);

    unsigned int f = get_timer_frequency();
    if (f) timer_freq = f;
    mlockall(MCL_CURRENT | MCL_FUTURE);

    if (as5600_init() == 0) { encoder_ok = 1; printf("AS5600 encoder ready\n"); }
    else printf("AS5600 encoder unavailable (position functions disabled)\n");

    printf("PARMCO CP3 controller: step mode FULL (%d steps/rev), timer %u Hz\n",
           STEPS_PER_REV, timer_freq);
    printf("Defaults: Manual, CW, %.0f RPM, stopped, not calibrated%s%s\n", RPM_DEFAULT,
           enc_polarity < 0 ? ", encoder inverted" : "", hold_enabled ? ", hold ON" : "");

    pthread_t th_motor, th_enc, th_ir, th_log, th_tel, th_rf;
    pthread_create(&th_enc, NULL, encoder_thread, NULL);
    pthread_create(&th_ir, NULL, ir_thread, NULL);
    pthread_create(&th_motor, NULL, motor_thread, NULL);
    pthread_create(&th_log, NULL, log_thread, NULL);
    pthread_create(&th_tel, NULL, telemetry_thread, NULL);
#ifndef PARMCO_SIM
    pthread_create(&th_rf, NULL, rfcomm_thread, NULL);
    if (ble_server_start(ble_line, ble_conn, secure) != 0)
        fprintf(stderr, "BLE server failed to start (iPhone app unavailable)\n");
    while (run_program) sleep_s(0.2);
#else
    /* Desktop simulation build only: commands from stdin. */
    if (getenv("SIM_SIGN0")) enc_sign = atoi(getenv("SIM_SIGN0"));
    (void)ble_line; (void)ble_conn; (void)secure; (void)rfcomm_thread;
    th_rf = th_tel;
    char line[MAX_LINE];
    while (run_program && fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (!strncmp(line, "SLEEP ", 6)) { sleep_s(atof(line + 6)); continue; }
        process_line(SRC_RFCOMM, line);
    }
    run_program = 0;
#endif

    printf("\nShutting down...\n");
    pthread_join(th_motor, NULL);
    coils_off();
    ble_server_stop();
    pthread_join(th_tel, NULL);
    pthread_join(th_log, NULL);
#ifndef PARMCO_SIM
    pthread_join(th_rf, NULL);
#endif
    pthread_join(th_ir, NULL);
    pthread_join(th_enc, NULL);
    char name[160];
    log_stop_file(name, sizeof(name));
    as5600_close();
    for (int i = 0; i < 4; i++) E4235_Select(motor_pins[i], 0);
    E4235_Select(PIN_ENABLE, 0);
    printf("PARMCO stopped safely.\n");
    return EXIT_SUCCESS;
}
