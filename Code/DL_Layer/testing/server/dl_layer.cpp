/*
 * dl_layer.cpp
 *
 *  Created on: Mar 20, 2012
 *      Author: six
 *
 *
 * Things to do:
 *		- Set up Timer
 *		- Continue to wrap head around piggyback
 *		- Queues, how to get buffer, push/pop
 *		- Queue to app_layer
 *		- Figure out how to reset, seq/ack counters *WHEN
 *		- Finalize how we want to encode/decode, packet/frame format
 *			i. setup basic frame in this
 */

#include "all.h"
#include <sys/time.h>

using namespace std;

//Window Size
#define MAX_SEQ 4
#define MAX_PKT 200
#define TIMEOUT_MAX 3000000 //fix later, 1 sec = 1000000

#define PHY 1
#define APP 2
#define TIME_OUT 3

typedef struct{
	int type; //0 for non ACK, 1 for ACK
	int seq_NUM;
	int ack_NUM;
	char data[MAX_PKT];
} frame;


//function prototypes
static void send_data(int frame_to_send, int frame_expected, string buff, int type);
int wait_for_event(void);
static bool between(int a, int b, int c);
void *time_disp(void* num);
int timeouts(void);
frame deconstruct_frame(string input);
long current_time();

//globals
long timers[4]={0};
int queued = 0;
int k;
string data;

queue<string> phy_send_q;
queue<string> phy_receive_q;
queue<string> dl_send_q;
queue<string> dl_receive_q;
queue<string> app_send_q;
queue<string> app_receive_q;
queue<string> window_q;

pthread_mutex_t mutex_phy_send = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_phy_receive = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_socket = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_app_send_q = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_app_receive_q = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_window_q = PTHREAD_MUTEX_INITIALIZER;

//Data Link Layer Master Thread
void *dl_layer_server(void *num){
	int frame_to_send = 0;
	int frame_expected = 0;
	int ack_expected = 0;
	int previous_frame_received=3;
	int rc;
	frame buffer;

	//Initalize Physical Layer
	pthread_t phy_thread;
	rc = pthread_create(&phy_thread, NULL, phy_layer_server, (void *) 1);
	if (rc){
		cout<<"Physical Layer Thread Failed to be created"<<endl;
		exit(1);
	}
	
	//Spawn Timers Status Thread, updates when timers change
	/*pthread_t thread;
        rc = pthread_create(&thread, NULL, time_disp , (void *) 1);
	if (rc){
		cout<<"Something bad happened with thread creation :("<<endl;
		exit(1);
	}*/
	

	//Wait for events to happen
	while (1) {
		cout<<"Waiting for event (DL)"<<endl;
		int event=wait_for_event();
		cout<<"Event Occurred: "<<event<<" (DL)"<<endl;
		switch (event) {

			//If PHY Layer receives message
			case (PHY):
				bzero(&buffer.data,sizeof(buffer.data));
				buffer = deconstruct_frame(phy_receive_q.front());
				cout<<"Message Deconstructed"<<endl;
				cout<<"Original: "<<phy_receive_q.front()<<endl;
				cout<<"Data: "<<buffer.data<<" seq_NUM: "<<buffer.seq_NUM<<" ack_NUM: "<<buffer.ack_NUM<<" Type: "<<buffer.type<<endl; 

				//ACK Received
				if (buffer.type){
					cout<<"Message is an ACK (DL)"<<endl;
					//Compare ACK seq number with older seq num in window
					cout<<"ACK Expected: "<<frame_expected<<" ACK Recvd: "<<buffer.seq_NUM<<endl;
					if (buffer.seq_NUM!=frame_expected){
						cout<<"ACK Out of order, Dropping (DL)"<<endl;
						phy_receive_q.pop();
						break;//Drop Packet
					}
					phy_receive_q.pop();
					window_q.pop();//Remove oldest frame from saved window
					queued--;
					frame_expected=(frame_expected++)%4;//Increment and wrap
				}
				else{//Data Frame Received
		
					//Correct order in sequence
					cout<<"Prev Seq: "<<previous_frame_received<<" Recvd Seq: "<<buffer.seq_NUM<<endl;
					if (buffer.seq_NUM==(previous_frame_received+1)%4){
						previous_frame_received=(previous_frame_received++)%4;
						dl_receive_q.push(buffer.data);
						phy_receive_q.pop();
						send_data(buffer.ack_NUM, buffer.seq_NUM, "ACK", 1);//Send ACK
					}
					else{//Drop Packet
						cout<<"Data Frame out order (DL)"<<endl;
						phy_receive_q.pop();
						break;
					}
				}


				//Check Cumulative ACKs
				//HOW DOES THIS WORK??????
				
				/*
				//If input is expected packet
				if (buffer.seq_NUM == frame_expected) {
					//pthread_mutex_lock( &mutex_app_receive_q);	
					//Add messages to app_layer_q, there will be a helper function that returns when something exists in q
					app_receive_q.push(buffer.data);
					//pthread_mutex_unlock( &mutex_app_receive_q);	
					frame_expected++;
				}

				k=0;
				while (between(ack_expected, buffer.ack_NUM, frame_to_send)) {
					//stop timer
					timers[k]=999999999999;//reset timer
					k++;
					if (queued==0)
						queued=3;
					else{
						queued--;
						window_q.pop();
					}
					ack_expected++;
					//pthread_mutex_lock(&mutex_phy_receive);
					phy_receive_q.pop();
					//pthread_mutex_unlock(&mutex_phy_receive);
				}

				*/


				break;

			//If APP Layer wants to send message
			case (APP):
				data = dl_send_q.front();
				dl_send_q.pop();
				window_q.push(data);//Save if needed for retransmission
				//Send buffer to physical layer
				//Include seq number for packing
				send_data(frame_to_send, frame_expected, data, 0);
				frame_to_send++;
				queued=(queued++)%4;//cycle to next q
				break;

			//If No ACK received, and timeout
			case (TIME_OUT):
				frame_to_send = ack_expected;
				//Reset N Frames
				if (queued==0){
					cout<<"Timeout incorrect Queue Size"<<endl;
					exit(1);
				}
				for (int i = 0; i < queued; i++){
					
					data = window_q.front();//Get oldest data to send first
					//Cycle Queue, so we push just oldest message to back, it will reach the front once all windowed messages are sent
					cout<<"Window Size: "<<window_q.size()<<endl;
					window_q.push(window_q.front());
					cout<<"Window Size: "<<window_q.size()<<endl;
					window_q.pop();
					cout<<"Window Size: "<<window_q.size()<<endl;


					//data = dl_send_q();
					send_data(frame_to_send, frame_expected, data, 0);
					frame_to_send++;
					//Reset Timer(s)
					cout<<"Reseting Timer: "<<i<<endl;
					cout<<"Timer: "<<timers[i]<<" Current: "<<current_time()<<" Diff: "<<(current_time()-timers[i])<<endl;
					timers[i]=current_time();
					//clear the queue
				}
				break;
		} //switch(event)
		cout<<"Event Completed (DL)"<<endl;//Done with that event

		if (queued > MAX_SEQ) cout << "FUCK (DL)"<<endl;
		//STOP putting stuff in the queue, or reset queue.

	} //while(1)
} //main

//SUPPORT FUNCTIONS//////////////////

//Trigger when event occurs
int wait_for_event(void){
	int event=0;
	while(event<1){
	    if (!phy_receive_q.empty())
		event=1;
	    else if (!dl_send_q.empty())
		event=2;
	    else if (timeouts())//Need a timeout function
		event=3;
	    //else
	      //  wait_for_event();
	}

	return event;
}

static void send_data(int frame_to_send, int frame_expected, string buff, int type){
	
	//Convert Integers to Characters
	char frame_expected_c[20];
	char frame_to_send_c[20];
	char type_c[20];
	sprintf(frame_expected_c, "%d", frame_expected);
	sprintf(frame_to_send_c, "%d", frame_to_send);
	sprintf(type_c, "%d", type);
		
	string tosend = string(type_c) + '\a' + frame_to_send_c + '\a' + frame_expected_c + '\a' + buff;
	phy_send_q.push(tosend);
}

//Returns true if a<=b<c, else false.
static bool between(int a, int b, int c){
	if (((a<=b)&&(b<c)) || ((c<a)&&(a<=b)) || ((b<c)&&(c<a)))
		return(true);
	else
		return(false);
}


//Check Timeout
int timeouts(void){

	long current=current_time();
	//Look at times
	for (int i=0;i<queued;i++)
		if ((current-timers[i])>TIMEOUT_MAX){
			cout<<"Timeout occured (DL)"<<endl;
			return 1;//Timeout occured
		}
	//cout<<"No timeouts"<<'\r';
	return 0;//No timeouts

}

//Print out timers
void *time_disp(void* num){
	int old_time[4]={0};
	//Update if times have changed
	while(1)
		if (old_time[0]!=timers[0] || old_time[1]!=timers[1] || old_time[2]!=timers[2] || old_time[3]!=timers[3]){
			cout<<"Timers 1:"<<timers[0]<<" Timers 2:"<<timers[0]<<" Timers 3:"<<timers[0]<<" Timers 4:"<<timers[0]<<'\r'; 
			for (int i=0;i<4;i++)
				old_time[i]=timers[i];//Update old times
		}
}

//Get current time
long current_time(){
	
	struct timeval tv;
        struct timezone tz;
        struct tm *tm;
        gettimeofday(&tv,&tz);
        tm=localtime(&tv.tv_sec);
	long total=(tm->tm_min*100000000+tm->tm_sec*1000000+tv.tv_usec);
	return total;
}


//Deconstruct Frame from PHY Layer
frame deconstruct_frame(string input){

	//CONSIDER USING p=strtok() for tokenization
        frame buffer_temp;
	bzero(&buffer_temp.data,200);//Clear out buffer structure
	bzero(&buffer_temp.seq_NUM,sizeof(int));
	bzero(&buffer_temp.ack_NUM,sizeof(int));
        int i=0;
        char temp[200];
	
        //Find  Type Num
        for (i=0;i<input.size();i++){
                if (input[i]=='\a'){
                        buffer_temp.type=atoi(temp);
                        i++;
			break;
                }
                else
                        temp[i]=input[i];
        }

	int p=i;
        //Find  Sequence Num
        while(i<input.size()){
                if (input[i]=='\a'){
                        buffer_temp.seq_NUM=atoi(temp);
			i++;
                        break;
                }
                else{
                        temp[i-p]=input[i];
			i++;
		}
        }

        //Find ACK Number
        char temp2[200];//clear
        i++;
        int j=i;
        while(i<input.size()){
                if (input[i]=='\a'){
                        buffer_temp.ack_NUM=atoi(temp2);
                        i++;
                        break;
                }
                else{
                        temp2[i-j]=input[i];
                        i++;
                }
        }
        //Find Message
        j=i;
        while(i<(input.size())){
                buffer_temp.data[i-j]=input[i];
                i++;
        }

      cout<<"SRQ NUM: "<<buffer_temp.seq_NUM<<endl;
	  return buffer_temp;
}

