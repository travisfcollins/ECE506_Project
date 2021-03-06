#include "all.h"

int PORT;
int vb_mode=0;

int main(int argc, char *argv[]){

	if (argc<2){
		cout<<"Need Port"<<endl;
		exit(1);
	}
	PORT=atoi(argv[1]);

	if (argc==3)
		vb_mode=atoi(argv[2]);
	

	//Start dl_layer
	//Initalize Physical Layer
        pthread_t phy_thread;
	int rc;
        rc = pthread_create(&phy_thread, NULL, phy_layer_server, (void *) 1);
        if (rc){
                cout<<"Physical Layer Thread Failed to be created"<<endl;
                exit(1);
        }
	
	//DO something
	int count=0;
	while(1){
		sleep(1);
		//cout<<"Doing something, I hope (APP)"<<endl;
		count++;
		if (count==12){
			cout<<"Sending Message (APP)"<<endl;
			pthread_mutex_lock(&mutex_dl_send[0]);
			dl_send_q[0].push("Server");
			pthread_mutex_unlock(&mutex_dl_send[0]);
			continue;
		}

		if (count==60)
			break;		

		for (int i=0;i<clients;i++){
			pthread_mutex_lock(&mutex_dl_receive[i]);
			if (!dl_receive_q[i].empty()){
				cout<<"APP LAYER Message: "<<dl_receive_q[i].front()<<endl;
				dl_receive_q[i].pop();
			}
			pthread_mutex_unlock(&mutex_dl_receive[i]);
			}

	}


	pthread_cancel(phy_thread);
	return 0;
}
