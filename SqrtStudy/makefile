CC = gcc
TARGET = sqrtStudy
OBJS = main.o methodOne.o methodTwo.o methodThree.o methodFour.o
LIBS = -lm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) -c $< -o $@
%.o: %.s
	$(CC) -c $< -o $@
clean:
	rm -f $(OBJS) $(TARGET)
