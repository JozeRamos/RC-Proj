// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define C_SET 0x03
#define A_SET 0x03
#define A_RES 0x01
#define C_DISC 0x0B
#define C_UA 0x07
#define FLAG 0x7E
#define C_RR 0x05
#define C_REJ 0x01


#define BUF_SIZE 256

int checkStates(char* buf, int length);

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

void trama(u_int16_t a,u_int16_t b,u_int16_t c,u_int16_t d,u_int16_t e,unsigned char buf[]){
    buf[0] = a;
    buf[1] = b;
    buf[2] = c;
    buf[3] = d;
    buf[4] = e;
}

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Create string to send
    unsigned char buf[500];
    trama(FLAG,A_SET,C_SET,A_SET ^ C_SET,FLAG,buf);
    
    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    //buf[5] = '\n';

    int bytes = write(fd, buf, 500);
    printf("%d bytes written\n", bytes);
    (void)signal(SIGALRM, alarmHandler);
    int cycle = 0;
    while (alarmCount < 4)
    {
        if (alarmCount == cycle){
            for(int i=0; i < 500; i++)
	            buf[i] = 0;
            bytes = read(fd, buf, 500);
            if (checkRecieve(buf, 500)){
                alarmEnabled = FALSE;
                break;
            }
            cycle++;
            trama(FLAG,A_SET,C_SET,A_SET ^ C_SET,FLAG,buf);
            int bytes = write(fd, buf, 500);
        }
        if (alarmEnabled == FALSE)
        {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }
    }
    if (alarmCount == 4){
    	printf("Timed out!!!");
    	exit(-1);
    }
    
    for(int i=0; i<5; i++)
	printf("%d ", buf[i]);
    printf("\n");
    if (buf[2] != C_UA){
	printf("Wrong Connection, buf[2] should be C_SET");
	exit(-1);
    }
    // Wait until all bytes have been written to the serial port
    sleep(1);

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}


int checkRecieve(char* buf, int length){
    int currentChar = 0;
    int state = 0; // 0 = START, 1 = FLAG, 2 = ADDRESS, 3 = CONTROL, 4 = BCC, 5 = STOPFLAG
    
    while(currentChar<length){
        //printf("%d",buf[currentChar]);
        switch(state){
            case 0: 
                if(buf[currentChar] == FLAG)
                    state = 1;
                
                currentChar++;
                break;
            
            case 1:
                if(buf[currentChar] == A_RES)
                    state = 2;
                else if(buf[currentChar] != FLAG)
                    state = 0;
                    
                currentChar++;
                break;
                
            case 2:
                if(buf[currentChar] == C_UA)
                    state = 3;
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;
                
            case 3:
                if(buf[currentChar] == buf[currentChar-1]^buf[currentChar-2])
                    state = 4;
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;
            
            case 4:
                if(buf[currentChar] == FLAG)
                    return TRUE;
                else 
                    state = 0;
                    
                currentChar++;
                break;
        }
    }
    return FALSE;
}

int checkData(char* buf, int length){
    int currentChar = 0;
    int state = 0; // 0 = START, 1 = FLAG, 2 = ADDRESS, 3 = CONTROL, 4 = BCC, 5 = STOPFLAG
    unsigned char BCC2;
    
    while(currentChar<length){
        switch(state){
            case 0: 
                if(buf[currentChar] == FLAG)
                    state = 1;
                
                currentChar++;
                break;
            
            case 1:
                if(buf[currentChar] == A_RES)
                    state = 2;
                else if(buf[currentChar] != FLAG)
                    state = 0;
                    
                currentChar++;
                break;
                
            case 2:
                if(buf[currentChar] == C_UA)
                    state = 3;
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;
                
            case 3:
                if(buf[currentChar] == buf[currentChar-1]^buf[currentChar-2])
                    state = 4;
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;

            case 4: // Reading data 
                BCC2 = buf[currentChar];

                for(int i=1; i<4; i++)
                    BCC2 = BCC2 ^ buf[currentChar+i];
                    
                currentChar = currentChar + 4;
                state = 5;
                break;

            case 5:
                if(buf[currentChar] == BCC2)
                    state = 6;
                else 
                    state = 0;
                    
                currentChar++;
                break;
            
            case 6:
                if(buf[currentChar] == FLAG)
                    return TRUE;
                else 
                    state = 0;
                    
                currentChar++;
                break;
        }
    }
    return FALSE;
}
