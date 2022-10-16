#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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


char * readFromFile();
char * createInformationFrame();
char * textToFrames();

int main(void){
    char * frame = createInformationFrame();
    for(int i = 0; i<1000; i++){
        printf("%d ", frame[i]);
        if(i%100 == 0) printf("\n\n");
    }
    printf("end");
    return 0;
}

char * createInformationFrame(){
    char * frame;
    long length;
    int c = 0;
    char * information = readFromFile(&length);
    __int16 BCC2 = 0x00;
    frame = malloc (length+6);
    frame[0] = FLAG;
    frame[1] = A_SET;
    frame[2] = 0x40;
    frame[3] = frame[1]^frame[2];

    for (int i = 0; i<length; i++){
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
    frame[length+4] = BCC2;
    frame[length+5] = FLAG;
    
    return frame;
}


char * readFromFile(long* length){
    char * buffer = 0;
    FILE * fp = fopen ("text.txt", "rb");
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

