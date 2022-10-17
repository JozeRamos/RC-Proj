#include <fcntl.h>
#include <stdio.h>
#include <math.h>
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

#define DATASIZE 500
int frame_num = 1;

char * readFromFile(long * length);
char * createInformationFrame(char * information, int size);
char * nextFrame(char * information);

int main(void){
    long textsize = 0;
    char * text = readFromFile(&textsize);
    int max_n = textsize/DATASIZE + (textsize % DATASIZE != 0); // max frame number approximated to the ceiling
    printf("Minimum Frames = %d\n\n\n", textsize, max_n);
    int size = DATASIZE;

    for(int i = 0; i < max_n; i++){
        if (frame_num == max_n) 
            size = textsize % DATASIZE; // if it's the last frame, it could be shorter than 500 chars

        char * frame = createInformationFrame(text+(i*DATASIZE), size);
        
        printf("\nFrame number %d:\n\n", frame_num);
        for(int j = 0; j<(size+6); j++){
            printf("%c", frame[j]);
            //if(i%100 == 0) printf("\n\n");
        }
        printf("\nend");
        frame_num++;
    }
    return 0;
}

char * createInformationFrame(char * information, int size){
    char * frame;
    int c = 0;
    __int16 BCC2 = 0x00;
    frame = malloc (size+6);
    frame[0] = FLAG;
    frame[1] = A_SET;
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

