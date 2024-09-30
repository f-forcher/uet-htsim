// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-        
#include "config.h"
#include <sstream>
#include <string.h>

#include <iostream>
#include <math.h>
#include "network.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "dcqcn_logger.h"
#include "clock.h"
#include "dcqcn.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "compositequeue.h"

// Simulation params

void exit_error(char* progr){
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] rate rtt" << endl;
    exit(1);
}

int main(int argc, char **argv) {
    EventList eventlist;
    eventlist.setEndtime(timeFromMs(10));
    Clock c(timeFromSec(50/100.), eventlist);

    srand(time(NULL));

    Packet::set_packet_size(4000);    
    linkspeed_bps SERVICE1 = speedFromMbps((uint64_t)100000);

    simtime_picosec RTT1=timeFromUs((uint32_t)10);
    mem_b BUFFER=memFromPkt(1200);

    stringstream filename(ios_base::out);
    filename << "logout.dat";
    cout << "Outputting to " << filename.str() << endl;
    Logfile logfile(filename.str(),eventlist);
  
    logfile.setStartTime(timeFromSec(0.0));

    Pipe pipe1(RTT1, eventlist); pipe1.setName("pipe1"); logfile.writeName(pipe1);
    Pipe pipe2(RTT1, eventlist); pipe2.setName("pipe2"); logfile.writeName(pipe2);

    LosslessOutputQueue::_ecn_enabled = true;
    LosslessOutputQueue::_K = Packet::data_packet_size() * 500;

    //LosslessOutputQueue queue(SERVICE1, BUFFER, eventlist,NULL); queue.setName("Queue1"); logfile.writeName(queue);
    CompositeQueue queue(SERVICE1,BUFFER,eventlist,NULL,64);
    LosslessOutputQueue queue2(SERVICE1, BUFFER, eventlist,NULL); queue2.setName("Queue2"); logfile.writeName(queue2);
    
    queue.set_ecn_thresholds(Packet::data_packet_size(), 100*Packet::data_packet_size());

    DCQCNSrc * src;
    DCQCNSink * snk;
    DCQCNSinkLoggerSampling sinkLogger(timeFromUs((uint32_t)10),eventlist);
    logfile.addLogger(sinkLogger);

    LosslessInputQueue::_low_threshold = memFromPkt(300);
    LosslessInputQueue::_high_threshold = memFromPkt(305);
    route_t* routeout;
    route_t* routein;

    int flow_count = 2;

    if (argc>1)
        flow_count = atoi(argv[1]);

    cout << "Flow count " << flow_count << endl;

    for (int i=0;i<flow_count;i++){
        src = new DCQCNSrc(NULL,NULL,eventlist,SERVICE1);
            //src->set_flowsize(20000000);
        src->setName("DCQCN" + ntoa(i)); 
        logfile.writeName(*src);
        snk = new DCQCNSink(eventlist); 
        ((RoceSink*)snk)->setName("DCQCNSink" + ntoa(i));
        logfile.writeName(*(RoceSink*)snk);

        // tell it the route
        routeout = new route_t();

        routeout->push_back(new LosslessInputQueue(eventlist, src)); 
        routeout->push_back(&queue); 
        routeout->push_back(&pipe1);
        routeout->push_back(snk);
        
        routein  = new route_t();
        routeout->push_back(&queue2); 
        routein->push_back(&pipe1);
        routein->push_back(src); 

        src->connect(routeout, routein, *snk,0);
        sinkLogger.monitorSink(snk);
    }

    // Record the setup
    int pktsize = Packet::data_packet_size();
    logfile.write("# pktsize="+ntoa(pktsize)+" bytes");
    //        logfile.write("# buffer2="+ntoa((double)(queue2._maxsize)/((double)pktsize))+" pkt");
    double rtt = timeAsSec(RTT1);
    logfile.write("# rtt="+ntoa(rtt));

    // GO!
    while (eventlist.doNextEvent()) {}
}

