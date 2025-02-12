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
#include "uec_logger.h"
#include "clock.h"
#include "uec.h"
#include "compositequeue.h"
#include "ecnqueue.h"

// Simulation params

void exit_error(char* progr){
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] rate rtt" << endl;
    exit(1);
}

simtime_picosec compute_latency(int cable_length){
    return timeFromNs(5.0 * cable_length);
}

int main(int argc, char **argv) {
    EventList eventlist;
    simtime_picosec end_time = timeFromUs(10000u);

    int cable_length[1000];

    //this controls whether RCCC uses a single PullPacer or multiple PullPacers for the connections in the dumbbell. Default is single PullPacer (flag set to false)
    bool dumbbell = false;
    bzero(cable_length,1000*sizeof(int));

    Clock c(timeFromSec(50/100.), eventlist);

    uint32_t cwnd = 50;

    int seed = 13;

    double pcie_rate = 1;

    mem_b queuesize = 35;
    mem_b ecn_threshold_min = 70;
    mem_b ecn_threshold_max = 70;
    uint32_t start_delta = 0;

    UecSink::_oversubscribed_cc = false;

    stringstream filename(ios_base::out);
    filename << "logout.dat";

    bool rts = false;

    uint32_t mtu = 4064;

    UecSink::_bytes_unacked_threshold = 3000;//force one ack per packet
    linkspeed_bps linkspeed = speedFromMbps((uint64_t)100000);

    simtime_picosec RTT1=timeFromUs((uint32_t)1);

    int flow_count = 1;
    int flow_size = 20000000;

    int i = 1;
    while (i<argc) {
        if (!strcmp(argv[i],"-o")) {
            filename.str(std::string());
            filename << argv[i+1];
            i++;
        } else if (!strcmp(argv[i],"-oversubscribed_cc")) {
              UecSink::_oversubscribed_cc = true;
        } else if (!strcmp(argv[i],"-conns")) {
            flow_count = atoi(argv[i+1]);
            assert (flow_count < 1000);
            cout << "no_of_conns "<<flow_count << endl;
            i++;
        } else if (!strcmp(argv[i],"-end")) {
            end_time = timeFromUs((uint32_t)atoi(argv[i+1]));
            cout << "endtime(us) "<< end_time << endl;
            i++;
        } else if (!strcmp(argv[i],"-debug")) {
            UecSrc::_debug = true;
        } else if (!strcmp(argv[i],"-dumbbell")) {
            dumbbell = true;
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
            UecSrc::_target_Qdelay = timeFromUs(atof(argv[i+1]));
            i++;
        } else if (!strcmp(argv[i],"-ecn_threshold")){
            // fraction of queuesize, between 0 and 1
            ecn_threshold_min = ecn_threshold_max = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-ecn")){
            // fraction of queuesize, between 0 and 1
            ecn_threshold_min = atoi(argv[i+1]);
            ecn_threshold_max = atoi(argv[i+2]);
            i+=2;
        } else if (!strcmp(argv[i],"-cable_lengths")){
            for (int j=0;j<flow_count;j++) {
                cable_length[j] = atoi(argv[i+1]);
                cout << "Cable length for sender " << j << " is " << cable_length[j] << "m" << endl;
                i++;
            }
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
        } else if (!strcmp(argv[i],"-pcie")){
            UecSink::_model_pcie = true;
            pcie_rate = atof(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-Ai")){
            OversubscribedCC::_Ai = atof(argv[i+1]);
            cout << "Setting oversubscribed additive increase to  " << OversubscribedCC::_Ai << endl;

            i++;
        } else if (!strcmp(argv[i],"-Md")){
            OversubscribedCC::_Md = atof(argv[i+1]);
            cout << "Setting oversubscribed multiplicative decrease to  " << OversubscribedCC::_Md << endl;

            i++;
        } else if (!strcmp(argv[i],"-alpha")){
            OversubscribedCC::_alpha = atof(argv[i+1]);
            cout << "Setting oversubscribed alpha to  " << OversubscribedCC::_alpha << endl;

            i++;
        } else if (!strcmp(argv[i],"-start_delta")){
            start_delta = atoi(argv[i+1]);
            cout << "Setting start_delta to  " << start_delta << "us" << endl;

            i++;
        } else if (!strcmp(argv[i],"-sender_cc")) {
            UecSrc::_sender_based_cc = true;
            cout << "sender based CC enabled "<<  endl;

            if (!strcmp(argv[i+1],"dctcp"))
                UecSrc::_sender_cc_algo = UecSrc::DCTCP;
            else if (!strcmp(argv[i+1],"nscc"))
                UecSrc::_sender_cc_algo = UecSrc::NSCC;
            else if (!strcmp(argv[i+1],"constant"))
                UecSrc::_sender_cc_algo = UecSrc::CONSTANT;
            else {
                cout << "Unknown congestion control type " << argv[i+1] << endl;
                exit_error(argv[0]);
            }

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

    OversubscribedCC::setOversubscriptionRatio(flow_count);
    //OversubscribedCC::_Ai = 0.1 / flow_count;

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

    CompositeQueue queue(linkspeed, queuesize, eventlist, &qs1, UecBasePacket::ACKSIZE);
    queue.setName("Queue1");
    logfile.writeName(queue);
    queue.set_ecn_thresholds(ecn_threshold_min,ecn_threshold_max);

    CompositeQueue queue2(linkspeed, queuesize, eventlist, NULL, UecBasePacket::ACKSIZE); queue2.setName("Queue2"); logfile.writeName(queue2);
    queue.set_ecn_thresholds(ecn_threshold_min,ecn_threshold_max);

    //figure out max cable length
    int max_cl = 0;
    for (i=0;i<flow_count;i++)
        if (max_cl < cable_length[i])
            max_cl = cable_length[i];

    UecSrc::_min_rto = 10*timeFromUs(2.0 + queuesize * 8 * 1000000 / linkspeed)+2 * compute_latency(max_cl);

    OversubscribedCC::_base_rtt = 2 * compute_latency(max_cl)+timeFromUs(2.0);

    cout << "Setting min RTO to " << timeAsUs(UecSrc::_min_rto) << endl;
    cout << "Max wire latency is " << timeAsUs(compute_latency(max_cl)+timeFromUs(2.0)) << endl;
    cout << "Speed is " << speedAsGbps(linkspeed) << "Gbps" << endl;
    cout << "BDP is " << 2*timeAsUs(compute_latency(max_cl)+2) * linkspeed / 8000000 << "B or " << (int)(2*timeAsUs(compute_latency(max_cl)+2) * linkspeed / 8000000)/mtu << "pkts" << endl;

    UecSrc* uecSrc;
    UecNIC* uecNic;
    UecSink* uecSnk;
    UecSinkLoggerSampling sinkLogger(timeFromUs((uint32_t)25),eventlist);

    logfile.addLogger(sinkLogger);
    route_t* routeout;
    route_t* routein;

    cout << "MTU: " << Packet::data_packet_size() << endl;
    cout << "Queuesize: " << queuesize << " bytes (" << queuesize/Packet::data_packet_size() << "packets)\n";
    cout << "Cwnd: " << cwnd << " packets\n";
    cout << "Linkspeed: " << linkspeed/1000000000 << "Gb/s\n";

    vector<UecSrc*> Uec_srcs;

    UecPullPacer* pacer = new UecPullPacer(linkspeed, 0.99, UecBasePacket::unquantize(UecSink::_credit_per_pull), eventlist, 1);

    for (int i=0;i<flow_count;i++){
        uecNic = new UecNIC(i, eventlist, linkspeed, 1);
        unique_ptr<UecMultipath> mp = make_unique<UecMpMixed>(256, UecSrc::_debug);
        uecSrc = new UecSrc(NULL, eventlist, move(mp), *uecNic, 1, rts);
        //UecSrc->setRouteStrategy(SINGLE_PATH);
        uecSrc->setCwnd(cwnd*Packet::data_packet_size());
        uecSrc->setMaxWnd(cwnd*Packet::data_packet_size());
        uecSrc->setFlowsize(flow_size);

        uecSrc->setName("Uec"+ntoa(i));
        logfile.writeName(*uecSrc);

        Uec_srcs.push_back(uecSrc);

        uecNic = new UecNIC(i, eventlist, linkspeed, 1);
        //uecSnk = new UecSink(NULL, linkspeed, 1, Packet::data_packet_size(), eventlist,*uecNic, 1);

        UecPullPacer * p = dumbbell?(new UecPullPacer(linkspeed, 0.99, UecBasePacket::unquantize(UecSink::_credit_per_pull), eventlist, 1)):pacer;
        uecSnk = new UecSink(NULL, p, *uecNic, 1);

        if (UecSink::_model_pcie){
            uecSnk->setPCIeModel(new PCIeModel(linkspeed * pcie_rate,Packet::data_packet_size(),eventlist,uecSnk->pullPacer()));
        }

        if (UecSink::_oversubscribed_cc){
            uecSnk->setOversubscribedCC(new OversubscribedCC(eventlist,uecSnk->pullPacer()));
        }

        ((DataReceiver*)uecSnk)->setName("UecSink");
        logfile.writeName(*(DataReceiver*)uecSnk);

        // tell it the route
        routeout = new route_t();
        // Uec expects each src host to have a FairPriorityQueue
        //routeout->push_back(new FairPriorityQueue(linkspeed, memFromPkt(1000),eventlist, NULL));
        routeout->push_back(new Pipe(compute_latency(cable_length[i]),eventlist));
        routeout->push_back(&queue);
        routeout->push_back(new Pipe(RTT1, eventlist));
        routeout->push_back(new CompositeQueue(linkspeed*1.1, queuesize, eventlist, NULL, UecBasePacket::ACKSIZE));
        routeout->push_back(new Pipe(0,eventlist));
        routeout->push_back(uecSnk->getPort(0));

        routein  = new route_t();
        routein->push_back(&pipe1);
        routein->push_back(&queue2);
        routein->push_back(new Pipe(compute_latency(cable_length[i]),eventlist));
        routein->push_back(&queue2);
        routein->push_back(uecSrc->getPort(0));

        //start time of the connections is delayed by start_delta to allow us to observe
        uecSrc->connectPort(0, *routeout, *routein, *uecSnk, start_delta*i);
        sinkLogger.monitorSink(uecSnk);
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
    for (size_t ix = 0; ix < Uec_srcs.size(); ix++) {
        new_pkts += Uec_srcs[ix]->stats().new_pkts_sent;
        rtx_pkts += Uec_srcs[ix]->stats().rtx_pkts_sent;
        bounce_pkts += Uec_srcs[ix]->stats().bounces_received;
    }
    cout << "New: " << new_pkts << " Rtx: " << rtx_pkts << " Bounced: " << bounce_pkts << endl;

}
