// Read from serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
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
#define C_RR_NR0 0x05
#define C_RR_NR1 0x85
#define C_REJ_NR0 0x01
#define C_REJ_NR1 0x81

#define BUF_SIZE 256

#define frameflag 0x7E
#define address1 0x03
#define address2 0x01

volatile int STOP = FALSE;
int Ns = 0;
int Nr = 1;

void swap();
int checkSupervision(unsigned char* buf, int length, u_int16_t ctrField);
int checkData(unsigned char buf[],unsigned char message[]);
void clearBuffer(unsigned char buf[]);

void swap(){
    if (Ns) Ns = 0;
    else Ns = 1;

    if (Nr) Nr = 0;
    else Nr = 1;
 }



void trama(u_int16_t a,u_int16_t b,u_int16_t c,u_int16_t d,u_int16_t e,unsigned char buf[]){
    buf[0] = a;
    buf[1] = b;
    buf[2] = c;
    buf[3] = d;
    buf[4] = e;
}

void clearBuffer(unsigned char buf[]){
    for (int i = 0; i < 500; i++){
        buf[i] = 0;
    }
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
    
    
    // Open serial port device for reading and writing and not as controlling tty
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
    newtio.c_cc[VMIN] = 2;  // Blocking read until 5 chars received

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

    // Loop for input
    unsigned char buf[500];
    
    //If the received trama is correct it moves forward, else it reads the trama sent again, if it reads it for more than 3 times it gets a error and exits
    int count = 0;
    int disconnecting = 0;
    int state = 0;
    int ignore = 0;
    unsigned char message[493];
    unsigned char trash[493];
    while (count < 3 && disconnecting == 0){
        if (read(fd, buf, 500)){
                
                
            if (checkData(buf,trash) && state == 1){
                state++;
                count = 0;
            }
            else if (state == 1){
                state--;
            }
            if (state == 2 && checkSupervision(buf, 500, C_DISC)){
                trama(FLAG,A_RES,C_DISC, A_RES^C_DISC, FLAG,buf);
                write(fd, buf, 500);
                clearBuffer(buf);
                disconnecting = 1;
            }
            else if (state == 2){
                //printf("bytes\n");
                clearBuffer(message);
                printf("here - %d\n", checkData(buf,message));
                switch (checkData(buf,trash))
                {
                case 0: // Repeated message, doesn't print
                    printf("Repeated message");
                    clearBuffer(buf);
                    count = 0;
                    if (Nr)
                        trama(FLAG,A_RES,C_RR_NR1,A_RES ^ C_RR_NR1, FLAG, buf);
                    else
                        trama(FLAG,A_RES,C_RR_NR0,A_RES ^ C_RR_NR0, FLAG, buf);
                    swap();
                    break;

                case 1: // Correct message, prints
                    count = 0;
                    for(int i=0; i < 494; i++)
                        printf("%c",message[i]);
                    printf("\n");
                    clearBuffer(buf);
                    if (Nr)
                        trama(FLAG,A_RES,C_RR_NR1,A_RES ^ C_RR_NR1, FLAG, buf);
                    else
                        trama(FLAG,A_RES,C_RR_NR0,A_RES ^ C_RR_NR0, FLAG, buf);
                    swap();
                    break;

                case 2: // Rejected message
                    printf("rejected message\n");
                    clearBuffer(buf);
                    if (Nr)
                        trama(FLAG,A_RES,C_REJ_NR1,A_RES ^ C_REJ_NR1, FLAG, buf);
                    else
                        trama(FLAG,A_RES,C_REJ_NR0,A_RES ^ C_REJ_NR0, FLAG, buf);
                    break;

                case 3: // Wrong header - No action, wait for timeout and resend
                    clearBuffer(buf);
                    printf("Wrong header\n");
                    break;
                }
                //for(int i=0; i < 20; i++)
                //    printf("%d -", buf[i]);
                //write(fd, buf, 500);
            }
            //for(int i=0; i < 5; i++)
            //    printf("%d -", buf[i]);
            //printf("\n");
            
            printf("\n Ns = %d Nr = %d \n", Ns, Nr);

            if (checkSupervision(buf, 500, C_SET) && state == 0){
                trama(FLAG,A_RES,C_UA,A_RES ^ C_UA,FLAG,buf);
                write(fd, buf, 500);
                state++;
                printf("good\n");
            }
            else if (!ignore){
                write(fd,buf,500);
            }
            else{
                write(fd,buf,500);
                count++;
            }
            
            printf("count %i   state %i  \n",count, state);

            
            clearBuffer(buf);
            
        }
    }
    if (count > 2){
        perror("Something went wrong...connection lost");
        exit(-1);
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if(disconnecting == 1){
        sleep(1);
        if (read(fd, buf, 500))
            if(checkSupervision(buf, 500, C_UA))
                printf("UA RECEIVED DISCONNECTING");
            else
                printf("UA NOT RECEIVED, DISCONNECTING");
            // if UA end
            //for(int i=0; i < 5; i++)
            //  printf("%d -", buf[i]);
        
    }

    clearBuffer(buf);
    printf("\n");

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}


int checkSupervision(unsigned char* buf, int length, u_int16_t ctrField){
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
                if(buf[currentChar] == A_SET)
                    state = 2;
                else if(buf[currentChar] != FLAG)
                    state = 0;
                    
                currentChar++;
                break;
                
            case 2:
                if(buf[currentChar] == ctrField)
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
                    
                break;
        }
    }
    return FALSE;
}

int checkData(unsigned char buf[], unsigned char message[]){
    int currentChar = 0;
    int state = 0; // 0 = START, 1 = FLAG, 2 = ADDRESS, 3 = CONTROL, 4 = BCC, 5 = STOPFLAG
    u_int16_t ctrField;
    u_int16_t oppositeCtrField;
    u_int16_t bcc = 0x00;
    if (Ns == 1){
        ctrField = 0x40;
        oppositeCtrField = 0x00;
    }
    else{
        ctrField = 0x00;
        oppositeCtrField = 0x40;
    }
    int count = 0;
    int messageC = 0;
    int maxState = 0; //Checks for maximum state, decides to ignore the frame if maxState is from 0 to 3
    while(currentChar<500){
        //printf("%i --  %d",currentChar, buf[currentChar]);
        switch(state){
            case 0:
                //printf("1\n");
                if(buf[currentChar] == FLAG)
                    state = 1;
                currentChar++;
                break;
            
            case 1:
                //printf("2\n");
                if (maxState < state)
                    maxState = state;
                if(buf[currentChar] == A_SET)
                    state = 2;
                else if(buf[currentChar] != FLAG)
                    state = 0;
                    
                currentChar++;
                break;
                
            case 2:
                //printf("3\n");
                if (maxState < state)
                    maxState = state;
                if(buf[currentChar] == ctrField)
                    state = 3;
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;
                
            case 3:
                //printf("4\n");
                if (maxState < state)
                    maxState = state;
                if(buf[currentChar] == buf[currentChar-1]^buf[currentChar-2]){
                    state = 4;
                }
                else if(buf[currentChar] == FLAG)
                    state = 1;
                else 
                    state = 0;
                    
                currentChar++;
                break;

            case 4: // Reading data
                //printf("5\n");
                if (maxState < state)
                    maxState = state;
                if (buf[currentChar] == FLAG){
                    //printf("FLAG");
                    if (count > 1){
                        bcc = bcc ^ message[messageC - 2];
                    }
                    else if (count == 1){
                        for (int i = messageC - 1; i < 494; i ++){
                            message[i] = 0;
                        }
                        return 1;
                    }
                    else
                        return 2;

                    if (buf[currentChar - 1] == bcc)
                        message[messageC - 1] = 0;
                        return 1;
                    return 2; 
                }
                if (buf[currentChar] == 0x7d){
                    if (buf[currentChar + 1] == 0x5e){
                        message[messageC] = 0x7e;
                    }
                    else{
                        message[messageC] = 0x7d;
                    }
                    currentChar++;
                }
                else{
                    message[messageC] = buf[currentChar];
                }
                if (count < 2)
                    count++;
                else
                    bcc = bcc ^ message[messageC - 2];
                messageC++;
                currentChar++;
                break;
        }
    }
    if (maxState < 4){
        return 3;
    }
    if (state == 4)
        return 2;
    printf("\nEND\n");
    return 0;
}
