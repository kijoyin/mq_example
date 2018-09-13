/* Example code for starting
 * 2 threads and synchronizing  
 * their operation using a message_queue.
 *
 * All code provided is as is 
 * and not completely tested
 *
 * Author: Aadil Rizvi
 * Date: 6/1/2016
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <util/util.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define CAR_SPEED "/car_speed"
#define CAR_CMD "/car_cmd"


pthread_t calculateSpeed;
pthread_t receiveCarCmd;

static struct mq_attr my_mq_attr;
static mqd_t car_mq, car_cmd;

static unsigned int counter;
static float speedKmPerHour;

void calculateSpeed_main(void);
void receiveCarCmd_main(void);

void sig_handler(int signum) {
    if (signum != SIGINT) {
        printf("Received invalid signum = %d in sig_handler()\n", signum);
        ASSERT(signum == SIGINT);
    }

    printf("Received SIGINT. Exiting Application\n");

    pthread_cancel(calculateSpeed);
    pthread_cancel(receiveCarCmd);

    mq_close(car_mq);
    mq_close(car_cmd);
    mq_unlink(CAR_SPEED);
    mq_unlink(CAR_CMD);

    exit(0);
}

int main(void) {
    pthread_attr_t attr;
    int status;
 
    signal(SIGINT, sig_handler);

    counter = 0;

    my_mq_attr.mq_maxmsg = 10;
    my_mq_attr.mq_msgsize = sizeof(counter);


    car_mq = mq_open(CAR_SPEED, \
                    O_CREAT | O_RDWR | O_NONBLOCK, \
                    0666, \
                    &my_mq_attr);
    car_cmd = mq_open(CAR_CMD, \
                    O_CREAT | O_RDWR | O_NONBLOCK, \
                    0666, \
                    &my_mq_attr);

    ASSERT(car_mq != -1);
    ASSERT(car_cmd != -1);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024*1024);
   
    
    printf("Creating car thread\n");
    status = pthread_create(&calculateSpeed, &attr, (void*)&calculateSpeed_main, NULL);
    if (status != 0) {
        printf("Failed to create carthread with status = %d\n", status);
        ASSERT(status == 0);
    }  
    
    printf("Creating car command thread\n");
    status = pthread_create(&receiveCarCmd, &attr, (void*)&receiveCarCmd_main, NULL);
    if (status != 0) {
        printf("Failed to create car command thread with status = %d\n", status);
        ASSERT(status == 0);
    }    

    pthread_join(calculateSpeed, NULL);
    pthread_join(receiveCarCmd, NULL);

    sig_handler(SIGINT);
    
    return 0;
}

void receiveCarCmd_main(void) {
    unsigned int exec_period_usecs;
    int status;

    exec_period_usecs = 1000000; /*in micro-seconds*/

    printf("Car cmd started started. Execution period = %d uSecs\n",\
                                           exec_period_usecs);
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    char buf[1024] = { 0 };
    int s, client, bytes_read;
    socklen_t opt = sizeof(rem_addr);

    // allocate socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // bind socket to port 1 of the first available 
    // local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = (uint8_t) 1;
    bind(s, (struct sockaddr *)&loc_addr, sizeof(loc_addr));

    // put socket into listening mode
    listen(s, 1);

    // accept one connection
    client = accept(s, (struct sockaddr *)&rem_addr, &opt);

    ba2str( &rem_addr.rc_bdaddr, buf );
    fprintf(stderr, "accepted connection from %s\n", buf);
    memset(buf, 0, sizeof(buf));

    // read data from the client
    bytes_read = read(client, buf, sizeof(buf));
    if( bytes_read > 0 ) {
        printf("received [%s]\n", buf);
    }

    // close connection
    close(client);
    close(s);
    while(1) {
        status = mq_send(car_cmd, (const char*)&counter, sizeof(counter), 1);
        ASSERT(status != -1);
        usleep(exec_period_usecs);
    }
}

void calculateSpeed_main(void) {
    unsigned int exec_period_usecs;
    int status;
    int recv_counter;

    exec_period_usecs = 10000; /*in micro-seconds*/

    printf("Car thread started. Execution period = %d uSecs\n",\
                                           exec_period_usecs);
    while(1) {
        status = mq_receive(car_mq, (char*)&recv_counter, \
                            sizeof(recv_counter), NULL);

        if (status > 0) {
            printf("RECVd MSG in Car: %d\n", recv_counter);
        }
 
        usleep(exec_period_usecs);
    }
}

