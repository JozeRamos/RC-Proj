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
#define C_RR_NR0 0x05
#define C_RR_NR1 0x85
#define C_REJ_NR0 0x01
#define C_REJ_NR1 0x81

#define BUFFER_SIZE 245

int checkSupervision(unsigned char* buf, int length, u_int8_t ctrField);
void clearBuffer(unsigned char buf[]);
int next_block_size(int count, int buffer_size);
void infoTrama(unsigned char buf[]);
void swap();


int next_block_size(int count, int buffer_size) {
return (count >= buffer_size)? buffer_size: count % buffer_size;
}

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount;
int Nr = 1;
int Ns = 0;

void swap(){
    if (Ns) Ns = 0;
    else Ns = 1;

    if (Nr) Nr = 0;
    else Nr = 1;
 }

// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    if (alarmCount != 4)
        printf("Alarm #%d\n", alarmCount);
}

void clearBuffer(unsigned char buf[]){
    for (int i = 0; i < 500; i++){
        buf[i] = 0;
    }
}

void trama(u_int8_t a,u_int8_t b,u_int8_t c,u_int8_t d,u_int8_t e,unsigned char buf[]){
    buf[0] = a;
    buf[1] = b;
    buf[2] = c;
    buf[3] = d;
    buf[4] = e;
}

void infoTrama(unsigned char buf[]){
    buf[0] = FLAG;
    buf[1] = A_SET;
    if (Ns == 1)
        buf[2] = 0x40;
    else
        buf[2] = 0x00;
    buf[3] = buf[1] ^ buf[2];
}

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 3)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort> <filename.txt>\n"
               "Example: %s /dev/ttyS1 text.txt\n",
               argv[0],
               argv[0]);
        exit(1);
    }


    /* check if file can be opened and is readable */
    int file = open(argv[2], O_RDONLY);
    if (file == -1) {
        printf("error: cannot open %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* get the file size */
    struct stat info;
    int ret = lstat(argv[2], &info);
    if (ret == -1) {
        printf("error: cannot stat %s\n", argv[2]);
        return EXIT_FAILURE;
    }


    int count = info.st_size;
    printf("real - %i\n", count);
    char buffer[BUFFER_SIZE];
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

    //printf("%d bytes written\n", bytes);
    (void)signal(SIGALRM, alarmHandler);
    int cycle = 0;
    int state = 0;
    int disconnectReceiver = 0;
    int connectionBad = 0;
    alarmCount = 0;
    unsigned char repeat[500];
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    while (alarmCount <3 && disconnectReceiver == 0)
    {
        
        if (alarmEnabled == FALSE)
            {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }
        if (alarmCount == cycle && count != 0 && state == 1) {
            cycle++;
            if (connectionBad == 0){
                clearBuffer(repeat);
                clearBuffer(buf);
                infoTrama(buf);
                int numOfBytes = 3;
                //printf("count - %i\n", count);
                u_int8_t bcc = 0x00;
                while (count > 0 && numOfBytes < 497 && connectionBad != 1){
                    pread(file, buffer, next_block_size(count, 1), (info.st_size - count));
                    //printf("count - %i\n",count);
                    count --;
                    numOfBytes ++;
                    //printf("%d  - %i", buffer[0], numOfBytes);
                    bcc = bcc ^ buffer[0];
                    if (buffer[0] == 0x7e){
                        buf[numOfBytes] = 0x7d;
                        numOfBytes++;
                        buf[numOfBytes] = 0x5e;
                    }
                    else if (buffer[0] == 0x7d){
                        buf[numOfBytes] = 0x7d;
                        numOfBytes++;
                        buf[numOfBytes] = 0x5d;
                    }
                    else{
                        buf[numOfBytes] = buffer[0];
                    }
                    //printf("byte - %d\n",buf[numOfBytes]);
                }
                buf[numOfBytes + 1] = bcc;
                buf[numOfBytes + 2] = FLAG;
                write(fd, buf, 500);

                for (int k = 0; k<500; k++){
                    printf("%c", buf[k]);
                    repeat[k] = buf[k];
                }

                clearBuffer(buf);
            }
            else
            {

                for(int i=0; i < 500; i++)
                    printf("%c", repeat[i]);
                write(fd, repeat, 500);
            }
            
            /*for(int i=0; i < 12; i++)
                printf("%d -", buf[i]);*/
            printf("\n Ns = %d Nr = %d \n",Ns, Nr);
            //printf("here\n");
            sleep(1);
            if (read(fd,buf,500) == 500){
                printf("\nGOOD READ\n");
                for(int i=0; i < 5; i++)
                    printf("%d -", buf[i]);

                if (checkSupervision(buf, 500, C_RR_NR0) == 1){
                    printf("Connection good for now");
                    alarmCount = 0;
                    cycle = 0;
                    swap();
                }
                else if (checkSupervision(buf, 500, C_RR_NR0) == 2){
                    printf("Message repeated");
                    alarmCount = 0;
                    cycle = 0;
                    count = count + 497;
                    swap();
                }
                else if (checkSupervision(buf, 500, C_REJ_NR0)){
                    printf("Message rejected by transmitter");
                    alarmCount = 0;
                    count = count + 497;
                }
                connectionBad = 0;
            }
            else {
                printf("\nBAD READ\n");
                connectionBad = 1;
            }
            /*for(int i=0; i < 500; i++)
                    printf("%i -- %c\n",i, buf[i]);
                printf("\n");*/
            
        }
        if (alarmCount == cycle && state == 0){
            clearBuffer(buf);
            /*bytes = read(fd, buf, 500);
            for(int i=0; i < 5; i++)
    	        printf("%d -", buf[i]);
            if (checkSupervision(buf, 500, C_UA)){
                alarmEnabled = FALSE;
                break;
            }*/
            cycle++;
            trama(FLAG,A_SET,C_SET,A_SET ^ C_SET,FLAG,buf);
            write(fd, buf, 500);
            clearBuffer(buf);
            sleep(1);
            if(read(fd, buf, 500))
                if (checkSupervision(buf, 500, C_UA)){
                    printf("Connection good ");
                    state++;
                    alarmCount = 0;
                    cycle = 0;
                }
            
            //printf("state - %i  cycle - %i  alarm - %i", state, cycle, alarmCount);
        }
        if (count < 1){
            while(disconnectReceiver == 0){
                clearBuffer(buf);
                trama(FLAG, A_SET,C_DISC, A_SET ^ C_DISC, FLAG, buf);
                write(fd, buf, 500);
                clearBuffer(buf);
                sleep(1);
                if(read(fd, buf, 500)){
                    if(checkSupervision(buf, 500, C_DISC)){
                        clearBuffer(buf);
                        printf("\nDisconnection received");
                        trama(FLAG, A_SET,C_UA, A_SET ^ C_UA, FLAG, buf);
                        write(fd, buf, 500);
                        disconnectReceiver = 1;
                    }
                }
            }
        }
    }
    if (alarmCount == 3){
    	printf("Timed out!!!");
    	exit(-1);
    }
    close(file);
    printf("\n");
    
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


int checkSupervision(unsigned char* buf, int length, u_int8_t ctrField){
    int currentChar = 0;
    int state = 0; // 0 = START, 1 = FLAG, 2 = ADDRESS, 3 = CONTROL, 4 = BCC, 5 = STOPFLAG
    if (ctrField == C_RR_NR0 && Nr == 1)
        ctrField = C_RR_NR1;
    if (ctrField == C_REJ_NR0 && Nr == 1)
        ctrField = C_REJ_NR1;


    while(currentChar<length){
        //printf("%d - ",buf[currentChar]);
        unsigned char x = buf[currentChar];
        int resend = 0;
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
                if(buf[currentChar] == ctrField)
                    state = 3;
                else if(buf[currentChar] == (ctrField ^ 0x80)){
                    resend = 1;
                    state = 3;
                }
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
                if(buf[currentChar] == FLAG){
                    if (resend == 0)
                        return 1; //Good frame with C_RR Nr byte equal to our Nr value. Can proceed
                    else 
                        return 2; //Received a confirmation for a frame that wasn't the one sent. Resending this one.
                }
                else 
                    state = 0;
                    
                currentChar++;
                break;
        }
    }
    return 0; //Frame not good
}
