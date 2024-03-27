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
#include "eqds_logger.h"
#include "clock.h"
#include "eqds.h"
#include "compositequeue.h"
#include "ecnqueue.h"

// Simulation params

void exit_error(char* progr){
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] rate rtt" << endl;
    exit(1);
}

int main(int argc, char **argv) {
    EventList eventlist;
    simtime_picosec end_time = timeFromSec(1);

    Clock c(timeFromSec(50/100.), eventlist);

    uint32_t cwnd = 50;

    int seed = 13;

    mem_b queuesize = 35; 
    mem_b ecn_threshold_min = 70; 
    mem_b ecn_threshold_max = 70; 

    stringstream filename(ios_base::out);
    filename << "logout.dat";

    bool rts = false;

    uint32_t mtu = 4064;

    EqdsSink::_bytes_unacked_threshold = 3000;//force one ack per packet
    linkspeed_bps linkspeed = speedFromMbps((uint64_t)100000);

    simtime_picosec RTT1=timeFromUs((uint32_t)3);

    int flow_count = 1;
    int flow_size = 2000000;

    int i = 1;
    while (i<argc) {
        if (!strcmp(argv[i],"-o")) {
            filename.str(std::string());
            filename << argv[i+1];
            i++;
        /*
        } else if (!strcmp(argv[i],"-oversubscribed_cc")) {
              NdpSink::_oversubscribed_congestion_control = true;
        */
        } else if (!strcmp(argv[i],"-conns")) {
            flow_count = atoi(argv[i+1]);
            cout << "no_of_conns "<<flow_count << endl;
            i++;
        } else if (!strcmp(argv[i],"-end")) {
            end_time = timeFromUs((uint32_t)atoi(argv[i+1]));
            cout << "endtime(us) "<< end_time << endl;
            i++;            
        } else if (!strcmp(argv[i],"-debug")) {
            EqdsSrc::_debug = true;
        } else if (!strcmp(argv[i],"-rts")) {
            rts = true;
            cout << "rts enabled "<< endl;
        } else if (!strcmp(argv[i],"-cwnd")) {
            cwnd = atoi(argv[i+1]);
            cout << "cwnd "<< cwnd << endl;
            i++;
        } else if (!strcmp(argv[i],"-q")){
            queuesize = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-target_qdelay")){
            EqdsSrc::_target_Qdelay = timeFromUs(atof(argv[i+1]));
            i++;
        } else if (!strcmp(argv[i],"-ecn_threshold")){
            // fraction of queuesize, between 0 and 1
            ecn_threshold_min = ecn_threshold_max = atoi(argv[i+1]); 
            i++;
        } else if (!strcmp(argv[i],"-ecn_thresholds")){
            // fraction of queuesize, between 0 and 1
            ecn_threshold_min = atoi(argv[i+1]); 
            ecn_threshold_max = atoi(argv[i+2]); 
            i+=2;
        } else if (!strcmp(argv[i],"-linkspeed")){
            // linkspeed specified is in Mbps
            linkspeed = speedFromMbps(atof(argv[i+1]));
            i++;
        } else if (!strcmp(argv[i],"-flowsize")){
            // linkspeed specified is in Mbps
            flow_size = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-seed")){
            seed = atoi(argv[i+1]);
            cout << "random seed "<< seed << endl;
            i++;
        } else if (!strcmp(argv[i],"-mtu")){
            mtu = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-sender_cc")) {
            EqdsSrc::_sender_based_cc = true;
            cout << "sender based CC enabled "<<  endl;

            if (!strcmp(argv[i+1],"dctcp"))
                EqdsSrc::_sender_cc_algo = EqdsSrc::DCTCP;
            else     
                EqdsSrc::_sender_cc_algo = EqdsSrc::SMARTT;
            
            i++;
        } else {
            cout << "Unknown parameter " << argv[i] << endl;
            exit_error(argv[0]);
        }   
        i++;
    }
    srand(seed);
    srandom(seed);
    eventlist.setEndtime(end_time);

    cout << "Outputting to " << filename.str() << endl;
    Logfile logfile(filename.str(),eventlist);
  
    logfile.setStartTime(timeFromSec(0.0));

    Packet::set_packet_size(mtu);

    queuesize = memFromPkt(queuesize);
    ecn_threshold_min = memFromPkt(ecn_threshold_min);
    ecn_threshold_max = memFromPkt(ecn_threshold_max);
    TrafficLoggerSimple logger;

    logfile.addLogger(logger);

    QueueLoggerSampling qs1 = QueueLoggerSampling(timeFromUs(10u),eventlist);logfile.addLogger(qs1);
    // Build the network

    Pipe pipe1(RTT1, eventlist); pipe1.setName("pipe1"); logfile.writeName(pipe1);
    Pipe pipe2(RTT1, eventlist); pipe2.setName("pipe2"); logfile.writeName(pipe2);

    CompositeQueue queue(linkspeed, queuesize, eventlist, &qs1, EqdsBasePacket::ACKSIZE);
    queue.setName("Queue1"); 
    logfile.writeName(queue);
    queue.set_ecn_thresholds(ecn_threshold_min,ecn_threshold_max);
    
    CompositeQueue queue2(linkspeed, queuesize, eventlist, NULL, EqdsBasePacket::ACKSIZE); queue2.setName("Queue2"); logfile.writeName(queue2);
    queue.set_ecn_thresholds(ecn_threshold_min,ecn_threshold_max);

    EqdsSrc* eqdsSrc;
    EqdsNIC* eqdsNic;
    EqdsSink* eqdsSnk;
    EqdsSinkLoggerSampling sinkLogger(timeFromUs((uint32_t)25),eventlist);

    logfile.addLogger(sinkLogger);
    route_t* routeout;
    route_t* routein;

    cout << "MTU: " << Packet::data_packet_size() << endl;
    cout << "Queuesize: " << queuesize << " bytes (" << queuesize/Packet::data_packet_size() << "packets)\n";
    cout << "Cwnd: " << cwnd << " packets\n";
    cout << "Linkspeed: " << linkspeed/1000000000 << "Gb/s\n";
 
    vector<EqdsSrc*> eqds_srcs;

    for (int i=0;i<flow_count;i++){
        eqdsNic = new EqdsNIC(i, eventlist, linkspeed, 1);
        eqdsSrc = new EqdsSrc(NULL,eventlist,*eqdsNic,rts,1);
        //eqdsSrc->setRouteStrategy(SINGLE_PATH);
        eqdsSrc->setCwnd(cwnd*Packet::data_packet_size());
        eqdsSrc->setFlowsize(flow_size);
        
        eqdsSrc->setName("EQDS"+ntoa(i)); 
        logfile.writeName(*eqdsSrc);

        eqds_srcs.push_back(eqdsSrc);

        eqdsSnk = new EqdsSink(NULL, linkspeed, 0.99, Packet::data_packet_size(), eventlist,*eqdsNic, 1); 
        eqdsSnk->setName("EqdsSink");
        logfile.writeName(*eqdsSnk);
        
        // tell it the route
        routeout = new route_t();
        // EQDS expects each src host to have a FairPriorityQueue
        routeout->push_back(new FairPriorityQueue(linkspeed, memFromPkt(1000),eventlist, NULL));
        routeout->push_back(&queue); 
        routeout->push_back(&pipe1);
        routeout->push_back(new CompositeQueue(linkspeed, queuesize, eventlist, NULL, EqdsBasePacket::ACKSIZE));
        routeout->push_back(new Pipe(RTT1, eventlist));
        routeout->push_back(eqdsSnk->getPort(0));
        
        routein  = new route_t();
        routein->push_back(&pipe1);
        routein->push_back(&queue2); 
        routein->push_back(&pipe1);
        routein->push_back(eqdsSrc->getPort(0)); 

        eqdsSrc->connectPort(0, *routeout, *routein, *eqdsSnk, timeFromUs(0.0));
        sinkLogger.monitorSink(eqdsSnk);
    }

    // Record the setup
    int pktsize = Packet::data_packet_size();
    logfile.write("# pktsize="+ntoa(pktsize)+" bytes");
    //        logfile.write("# buffer2="+ntoa((double)(queue2._maxsize)/((double)pktsize))+" pkt");
    double rtt = timeAsSec(RTT1);
    logfile.write("# rtt="+ntoa(rtt));

    // GO!
    while (eventlist.doNextEvent()) {}

    cout << "Done" << endl;

    int new_pkts = 0, rtx_pkts = 0, bounce_pkts = 0;
    for (size_t ix = 0; ix < eqds_srcs.size(); ix++) {
        new_pkts += eqds_srcs[ix]->_new_packets_sent;
        rtx_pkts += eqds_srcs[ix]->_rtx_packets_sent;
        bounce_pkts += eqds_srcs[ix]->_bounces_received;
    }
    cout << "New: " << new_pkts << " Rtx: " << rtx_pkts << " Bounced: " << bounce_pkts << endl;

}
