#include <iostream>
#include <string>
#include <queue>
#include <cfloat>  //DBL_MAX
#include <cstdlib> //drand48() a Linux specific thing I think
#include <cmath> //log()
#include <vector>

#define lambda .9
#define N 25
#define MINBYTES 64
#define MAXBYTES 1518
#define PROPDELAY 0.00001
#define TRANRATE 100 //measured in megabits per second
#define TRIALS 1000000

using namespace std;

//Assumption: Cannot access a frames packet until the whole frame has been sent.
//If you can access packets as sent before entire frame is sent, it changes the timing of packet arrivals, and therefore average packet arrival time.
typedef enum
{
  ARRIVAL, DEPARTURE, BLANK, TOKEN, FRAME
} EventType;

//Will containt the event type (ARRIVAL, TOKEN, FRAME)
//Has time the event occurs, the source ID and destination ID
//(ARRIVAL: source host and dest host, TOKEN: src means old host, dest is new host, FRAME: src is source host, dest is current new host)
//Actual packets for ARRIVAL events generated upon dequeue from the priority queue. As it is random, it should not matter for the simulation the time at which the packet is generated.
class Event
{
  public:
  Event(EventType type, double t, int src, int dest):eType(type), time(t), srcID(src), destID(dest){}
  Event():eType(BLANK), time(DBL_MAX), srcID(-1), destID(-1) {}
  Event(const Event &rhs): eType(rhs.eType), time(rhs.time), srcID(rhs.srcID), destID(rhs.destID){};
  EventType eType;
  double time;
  int srcID;
  int destID;
  bool operator<(Event const &rhs) const
  {
    return !(this->time < rhs.time);   
  }
  Event operator=(Event const &rhs)
  {
	  this->eType = rhs.eType;
	  this->time = rhs.time;
	  this->srcID = rhs.srcID;
	  this->destID = rhs.destID;
	  return *this;
  }
};

//Packet class, represents a packet. Has its start time (for computation of average packet arrival time), size (won't be used in this program), destID, and srcID.
class Packet
{
  public:
    Packet(double time, int bytes): startTime(time), size(bytes){}
	Packet(): startTime(-1), size(-1){}
	double startTime;
    int size;
};



//When you want to set a Frame back to its default values, have frame = Frame();
//Contains the totalBytes for determination of how long it would take to send to the next host, and for the throughput calculation. 
//totalPackets will likely not be used outside of determining if the host has something to send.
//packetVectors contains a vector of packets for each host, to see if there is content for that host.
//bytesPerHost will be used to keep track of successful number of bytes transfers (for throughput).
//Each time a FRAME event occurs, and it is at a correct host, we increment total by this number
class Frame
{
  public:
  Frame():totalBytes(0), totalPackets(0), packetVectors(N, vector<Packet>()) {}
  int totalBytes;
  int totalPackets;
  vector< vector<Packet> > packetVectors;
  Frame operator=(const Frame &rhs)
  {
	this->totalBytes = rhs.totalBytes;
	this->totalPackets = rhs.totalPackets;
	this->packetVectors = rhs.packetVectors;
	return *this;
  }
  void insert(Packet pckt, int destID)
  {
	  totalBytes += pckt.size;
	  totalPackets += 1;
	  packetVectors[destID].push_back(pckt);
  }
  double delay()
  {
	  double propDelay = PROPDELAY;
	  double byteTranRate = TRANRATE / 8.0; //convert megabits per second to megabytes per second
	  double tranDelay = (totalBytes * 0.000001) / byteTranRate; //convert totaLBytes to MB then find time to tranfer
//	  if( 0.001 <= propDelay + tranDelay)
//		  cout << propDelay + tranDelay << endl;
	  return propDelay + tranDelay;
  }
  
  bool hasDataFor(int ID)
  {
	  return packetVectors[ID].size() > 0;
  }
  
};

//
class Host
{
  public:
    Host():frame(){}

    Frame frame;

	bool isFrameEmpty()
    {
      return frame.totalPackets == 0;
    }
	void resetFrame()
	{
	  this->frame = Frame();
	}
	void insertPacket(Packet pckt, int destID)
	{
	  frame.insert(pckt, destID);
	}
};


double nedt(double rate);
int generatePacketLength(int min = MINBYTES, int max = MAXBYTES); //random number between 64-1518 bytes
int generateDestination(int ID, int numHosts = N); //puts own ID so it doesn't send to self, also N for number of hosts

int main()
{
  priority_queue<Event> GEL;
  double currTime(0); //keeps track of current time
  int totalBytesSent(0);
  double totalPacketDelay(0); //Sum of all the delays;
  double totalPackets(0); //Successful packets sent (not including ones that never got sent)
  vector<Host> hosts(N, Host());
  Frame currFrame; //has the current frame being passed around. not used when token is out.

  int packetsWaiting(0);
  bool tokenOut(false);
 
  for(int i = 0; i < N; i++)
  {
    GEL.push(Event(ARRIVAL, currTime + nedt(lambda), i, generateDestination(i))); //first arrival event
  }

  for(int i = 0; i < TRIALS; i++)
  {
  //need to ask if it sends the whole frame at once, or if we are sending one packet at a time
  //will do as a whole fram for now, but have the "time" be calculated. Transmission delay will be the same every time, prop delay might have to be cumalative. 
    if(GEL.empty())
    {
      cout << "Houston, we have a problem." << endl;
    }
    Event temp(GEL.top());
    GEL.pop();
	currTime = temp.time;
	
	switch(temp.eType)
	{
		case ARRIVAL:
			GEL.push(Event(ARRIVAL, currTime + nedt(lambda), temp.srcID, generateDestination(temp.srcID))); //Generate a new arrival for this host so it can keep trying to transmite new things.
			hosts[temp.srcID].insertPacket(Packet(currTime, generatePacketLength()), temp.destID);
			if(hosts[temp.srcID].isFrameEmpty())
			{
				cout << "Just inserted and it is still empty." << endl;
			}
			packetsWaiting++;
			if(!tokenOut)
			{
				GEL.push(Event(TOKEN, currTime, N - 1, 0));
				tokenOut = true;
			}
			break;
		case DEPARTURE:
			cout << "Departure Event. Should not happen anymore." << endl;
			break;
		case TOKEN:
		//	i--; //Otherwise, it would just move tokens 100000 times before a packet can be sent.
			if(hosts[temp.destID].isFrameEmpty()) //if no packets to send
				GEL.push(Event(TOKEN, currTime + PROPDELAY, temp.destID, (temp.destID + 1) % N ));
			else //Host does have a frame to send
			{
				if(i < 2)
					cout << "arrival time: " << currTime << endl;
				currFrame = hosts[temp.destID].frame;
				packetsWaiting -= currFrame.totalPackets;
				for(int j = 1; j< N; j++)
				{
					if(currFrame.hasDataFor((temp.destID + j) % N )) //if the frame contains data for this host
					{
						for(auto packet : currFrame.packetVectors[(temp.destID + j) % N])
						{
		//					cout << "Delay: " << currTime - packet.startTime << " + " << (j * currFrame.delay()) << " = " << currTime - packet.startTime + (j * currFrame.delay()) << endl << endl;
							totalPacketDelay = totalPacketDelay + currTime - packet.startTime + (j * currFrame.delay());
							totalBytesSent += packet.size;
							totalPackets++;
						}
					}
				}
				GEL.push(Event(FRAME, currTime + (N-1)*currFrame.delay(), temp.destID, (temp.destID + 1) % N));
				hosts[temp.destID].resetFrame();
			}
			break;
		case FRAME: //must check if ID is own ID. If so, send out a token instead
			if(packetsWaiting != 0)
			{
				tokenOut = true;
				GEL.push(Event(TOKEN, currTime + PROPDELAY, temp.destID, (temp.destID + 1) % N ));
			}
			else
				tokenOut = false;
			break;
		case BLANK:
			cout << "Somehow got a BLANK event. Should never happen." << endl;
			break;
		default:
			cout << "Some how did not get any event type. Should never ever happen." << endl;
	}
	

  }

  cout << "Time measured in seconds"<< endl;
  cout << "Throughput: " << totalBytesSent << " / " << currTime << " = " << ((double) totalBytesSent) / ((double) currTime) << " bytes per second" << endl;
  cout << "Average packet delay: " << totalPacketDelay << " / " << totalPackets << " = " << ((double) totalPacketDelay) / ((double)totalPackets) <<" seconds" << endl;
}

double nedt(double rate)
{
  double u;
  u = drand48();
  return ((-1/rate)*log(1-u));
}

//had to remove default values in the implementation (but not the prototype) because compiler was yelling at me, may cause problems.
int generatePacketLength(int min, int max) //random number between 64-1518 bytes
{
  return ((max - min) * drand48()) + min;
}

//had to remove default values in the implementation (but not the prototype) because compiler was yelling at me, may cause problems.
int generateDestination(int ID, int numHosts) //puts own ID so it doesn't send to self, also N for number of hosts
{
  int randID;
  do
  {
    randID = ((numHosts - 1) * drand48());
  } while (randID == ID);
  return randID;
}