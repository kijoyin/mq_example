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
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#define CAR_SPEED "/car_speed"
#define CAR_CMD "/car_cmd"
#define CHANNEL 4
#define QUEUE 10



pthread_t calculateSpeed;
pthread_t receiveCarCmd;

static struct mq_attr my_mq_attr;
static mqd_t car_mq, car_cmd;

static unsigned int counter;
static float speedKmPerHour;

void calculateSpeed_main(void);
void receiveCarCmd_main(void);
sdp_session_t *register_service(uint8_t rfcomm_channel);
int init_server();


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
    init_server();
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
sdp_session_t *register_service(uint8_t rfcomm_channel) {

	/* A 128-bit number used to identify this service. The words are ordered from most to least
	* significant, but within each word, the octets are ordered from least to most significant.
	* For example, the UUID represneted by this array is 00001101-0000-1000-8000-00805F9B34FB. (The
	* hyphenation is a convention specified by the Service Discovery Protocol of the Bluetooth Core
	* Specification, but is not particularly important for this program.)
	*
	* This UUID is the Bluetooth Base UUID and is commonly used for simple Bluetooth applications.
	* Regardless of the UUID used, it must match the one that the Armatus Android app is searching
	* for.
	*/
	uint32_t svc_uuid_int[] = { 0x01110000, 0x00100000, 0x80000080, 0xFB349B5F };
	const char *service_name = "Armatus Bluetooth server";
	const char *svc_dsc = "A HERMIT server that interfaces with the Armatus Android app";
	const char *service_prov = "Armatus";

	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid,
	       svc_class_uuid;
	sdp_list_t *l2cap_list = 0,
	            *rfcomm_list = 0,
	             *root_list = 0,
	              *proto_list = 0,
	               *access_proto_list = 0,
	                *svc_class_list = 0,
	                 *profile_list = 0;
	sdp_data_t *channel = 0;
	sdp_profile_desc_t profile;
	sdp_record_t record = { 0 };
	sdp_session_t *session = 0;

	// set the general service ID
	sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
	sdp_set_service_id(&record, svc_uuid);

	char str[256] = "";
	sdp_uuid2strn(&svc_uuid, str, 256);
	printf("Registering UUID %s\n", str);

	// set the service class
	sdp_uuid16_create(&svc_class_uuid, SERIAL_PORT_SVCLASS_ID);
	svc_class_list = sdp_list_append(0, &svc_class_uuid);
	sdp_set_service_classes(&record, svc_class_list);

	// set the Bluetooth profile information
	sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
	profile.version = 0x0100;
	profile_list = sdp_list_append(0, &profile);
	sdp_set_profile_descs(&record, profile_list);

	// make the service record publicly browsable
	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups(&record, root_list);

	// set l2cap information
	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);

	// register the RFCOMM channel for RFCOMM sockets
	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);

	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(&record, access_proto_list);

	// set the name, provider, and description
	sdp_set_info_attr(&record, service_name, service_prov, svc_dsc);

	// connect to the local SDP server, register the service record,
	// and disconnect
	//sudo chmod 777 /var/run/sdp
	//https://askubuntu.com/questions/775303/unified-remote-bluetooth-could-not-connect-to-sdp
	session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
	sdp_record_register(session, &record, 0);

	// cleanup
	sdp_data_free(channel);
	sdp_list_free(l2cap_list, 0);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svc_class_list, 0);
	sdp_list_free(profile_list, 0);

	return session;
}

int init_server() {
	int port = 3, result, sock, client, bytes_read, bytes_sent;
	struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
	char buffer[1024] = { 0 };
	socklen_t opt = sizeof(rem_addr);

	// local bluetooth adapter
	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_bdaddr = *BDADDR_ANY;
	loc_addr.rc_channel = (uint8_t) port;

	// register service
	sdp_session_t *session = register_service(port);

	// allocate socket
	sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	printf("socket() returned %d\n", sock);

	// bind socket to port 3 of the first available
	result = bind(sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
	printf("bind() on channel %d returned %d\n", port, result);

	// put socket into listening mode
	result = listen(sock, 1);
	printf("listen() returned %d\n", result);

	//sdpRegisterL2cap(port);

	// accept one connection
	printf("calling accept()\n");
	client = accept(sock, (struct sockaddr *)&rem_addr, &opt);
	printf("accept() returned %d\n", client);

	ba2str(&rem_addr.rc_bdaddr, buffer);
	fprintf(stderr, "accepted connection from %s\n", buffer);
	memset(buffer, 0, sizeof(buffer));
	
	// read data from the client
    bytes_read = read(client, buffer, sizeof(buffer));
    if( bytes_read > 0 ) {
        printf("received [%s]\n", buffer);
    }

	return client;
}

char *read_server(int client) {
	// read data from the client
	char input[1024] = { 0 };
	int bytes_read;
	bytes_read = read(client, input, sizeof(input));
	if (bytes_read > 0) {
		printf("received [%s]\n", input);
		return input;
	} else {
		return "";
	}
}

void write_server(int client, char *message) {
	// send data to the client
	char messageArr[1024] = { 0 };
	int bytes_sent;
	sprintf(messageArr, message);
	bytes_sent = write(client, messageArr, sizeof(messageArr));
	if (bytes_sent > 0) {
		printf("sent [%s]\n", messageArr);
	}
}


