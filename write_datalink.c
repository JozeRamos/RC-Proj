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
#define ESCAPE 0x7d
#define FLAG_ESCAPE 0x5e
#define ESCAPE_ESCAPE 0x5d


#define BUF_SIZE 256
#define DATASIZE 494
#define FRAMESIZE DATASIZE+6

int frame_num = 1;

int checkSupervision(char* buf, int length, u_int16_t ctrField);
void clearBuffer(unsigned char buf[]);
char * readFromFile(long * length, char* txt_filename);
char * createInformationFrame(char * information, int size);
char * nextFrame(char * information);

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;
int Nr = 1;
int Ns = 0;

// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

void clearBuffer(unsigned char buf[]){
    for (int i = 0; i < FRAMESIZE; i++){
        buf[i] = 0;
    }
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
    char *txt_filename = argv[2];

    if (argc < 3)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort> <filename.txt>\n"
               "Example: %s /dev/ttyS1 <loremipsum.txt>\n",
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
    printf("Sending the S frame!\n");
    
    // S Frame
    unsigned char buf [FRAMESIZE];
    
    trama(FLAG, A_SET, C_SET, A_SET^C_SET, FLAG, buf);
    
    int bytes = write(fd, buf, 500);
    printf("%d bytes written\n", bytes);
    (void)signal(SIGALRM, alarmHandler);
    int cycle = 0;
    while (alarmCount < 3)
    {
        if (alarmCount == cycle){
            clearBuffer(buf);
            bytes = read(fd, buf, 500);
            if (checkSupervision(buf, 500, C_UA)){
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
    if (alarmCount == 3){
    	printf("Timed out!!!");
    	exit(-1);
    }
    
    printf("\n");
    
    // Wait until all bytes have been written to the serial port
    sleep(1);


    // Create the frames
    long textsize = 0;
    char * frame;
    char * text = readFromFile(&textsize, txt_filename);
    int max_n = textsize/DATASIZE + (textsize % DATASIZE != 0); // max frame number int
    
    printf("Minimum Frames = %d\n\n\n", max_n);
    
    int size = DATASIZE;
    
    printf("Sending the I frame!\n");
    
    for(int i = 0; i < max_n; i++){
        if (frame_num == max_n) 
            size = textsize % DATASIZE; // if it's the last frame, it could be shorter than 500 chars

        frame = createInformationFrame(text+(i*DATASIZE), size);
        
            int bytes = write(fd, frame, FRAMESIZE);
            
            printf("\nFrame number %d:\n\n", frame_num);
            for(int j = 0; j<(size+6); j++){
                printf("%d ", frame[j]);
                //if(i%100 == 0) printf("\n\n");
            }
            printf("\nend");
            
            printf("%d bytes written\n", bytes);
            (void)signal(SIGALRM, alarmHandler);
            int cycle = 0;
            while (alarmCount < 3)
            {
                if (alarmCount == cycle){
                    clearBuffer(buf);
                    bytes = read(fd, buf, FRAMESIZE);
                    if (checkSupervision(buf, FRAMESIZE, C_UA)){
                        alarmEnabled = FALSE;
                        break;
                    }
                    cycle++;
                    int bytes = write(fd, frame, FRAMESIZE);
                }
                if (alarmEnabled == FALSE)
                {
                    alarm(3); // Set alarm to be triggered in 3s
                    alarmEnabled = TRUE;
                }
            }
            if (alarmCount == 3){
            	printf("Timed out!!!");
            	exit(-1);
            }
            
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
        frame_num++;
    }
    

    return 0;
}


int checkSupervision(char* buf, int length, u_int16_t ctrField){
    int currentChar = 0;
    int state = 0; // 0 = START, 1 = FLAG, 2 = ADDRESS, 3 = CONTROL, 4 = BCC, 5 = STOPFLAG
    if ((ctrField == C_RR || ctrField == C_REJ) && Nr == 1){
        ctrField = 0x80 | ctrField;
    }
    while(currentChar<length){
        //printf("%d - ",buf[currentChar]);
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

char * createInformationFrame(char * information, int size){
    char * frame;
    int c = 0;
    u_int16_t BCC2 = 0x00;
    frame = malloc (size+6);
    frame[0] = FLAG;
    frame[1] = A_SET;
    if(Ns == 0) 
        frame[2] = 0x00;
    else
        frame[2] = 0x40;
    frame[3] = frame[1]^frame[2];

    for (int i = 0; i<size; i++){
        BCC2 = BCC2 ^ information[i];
        if(information[i] == FLAG){
            frame[4+i+c] = ESCAPE;
            frame[4+i+1+c] = FLAG_ESCAPE;
            c++;
        }
        else if(information[i] == ESCAPE){
            frame[4+i+c] = ESCAPE;
            frame[4+i+1+c] = ESCAPE_ESCAPE;
            c++;
        }
        else{
            frame[4+i+c] = information[i];
        }
    }
    frame[size+4] = BCC2;
    frame[size+5] = FLAG;
    
    return frame;
}


char * readFromFile(long* length, char* txt_filename){
    char * buffer = 0;
    FILE * fp = fopen (txt_filename, "rb");
    if (fp != NULL){
        fseek (fp, 0, SEEK_END);
        *length = ftell (fp);
        fseek (fp, 0, SEEK_SET);
        buffer = malloc ((*length)+1);

        if (buffer)
        {
            fread (buffer, 1, *length, fp);
        }
        fclose (fp);
        buffer[*length] = '\0';
        return buffer;
    }
}
