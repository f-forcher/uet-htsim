// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "uec.h"
#include <math.h>
#include "circular_buffer.h"
#include "uec_logger.h"

using namespace std;

// Static stuff

// _path_entropy_size is the number of paths we spray across.  If you don't set it, it will default
// to all paths.
uint32_t UecSrc::_path_entropy_size = 256;
int UecSrc::_global_node_count = 0;

/* _min_rto can be tuned using setMinRTO. Don't change it here.  */
simtime_picosec UecSrc::_min_rto = timeFromUs((uint32_t)DEFAULT_UEC_RTO_MIN);

mem_b UecSink::_bytes_unacked_threshold = 16384;
int UecSink::TGT_EV_SIZE = 7;

/* if you change _credit_per_pull, fix pktTime in the Pacer too - this assumes one pull per MTU */
UecBasePacket::pull_quanta UecSink::_credit_per_pull = 8;  // uints of typically 512 bytes

/* this default will be overridden from packet size*/
uint16_t UecSrc::_hdr_size = 64;
uint16_t UecSrc::_mss = 4096;
uint16_t UecSrc::_mtu = _mss + _hdr_size;

bool UecSrc::_debug = false;

bool UecSrc::_sender_based_cc = false;
bool UecSrc::_receiver_based_cc = true;

UecSrc::Sender_CC UecSrc::_sender_cc_algo = UecSrc::NSCC;

double UecSrc::_scaling_factor_a = 1; //for 400Gbps. cf. spec must be set to BDP/(100Gbps*12us)
uint32_t UecSrc::_qa_scaling = 1; //quick adapt scaling - how much of the achieved bytes should we use as new CWND?
double UecSrc::_gamma = 0.8; //used for aggressive decrease
uint32_t UecSrc::_pi = 5000 * _scaling_factor_a;//pi = 5 * mtu_size for 400Gbps; proportional increase constant
double UecSrc::_alpha = UecSrc::_scaling_factor_a * 1000 * 4000 / timeFromUs(6u);
//double UecSrc::_scaling_c = 4000 / 6 / UecSrc::_pi;//scaling_c = mtu_size/(((target_rtt-base_rtt)* pi) ; UNUSED!
double UecSrc::_fi = 1; //fair_increase constant
double UecSrc::_fi_scale = .25 * UecSrc::_scaling_factor_a;

double UecSrc::_ecn_alpha = 0.125;
double UecSrc::_delay_alpha = 0.125;

simtime_picosec UecSrc::_adjust_period_threshold = timeFromUs(12u);

#define FAIR_DECREASE

#ifdef FAIR_DECREASE
    double UecSrc::_eta = 0;
    double UecSrc::_fd = 0.8; //fair_decrease constant
    simtime_picosec UecSrc::_target_Qdelay = timeFromUs(6u);
    double UecSrc::_ecn_thresh = 0.2;
    uint32_t UecSrc::_adjust_bytes_threshold = 32000*(_target_Qdelay/timeFromUs(12u));

#else
    double UecSrc::_fd = 0.0; //fair_decrease constant
    simtime_picosec UecSrc::_target_Qdelay = timeFromUs(6u);
    double UecSrc::_eta = 0.15*(_target_Qdelay/timeFromUs(12u)) * 4000 * UecSrc::_scaling_factor_a;
    double UecSrc::_ecn_thresh = 0;
    uint32_t UecSrc::_adjust_bytes_threshold = 32000*(_target_Qdelay/timeFromUs(12u));

    
#endif

double UecSrc::_qa_threshold = 4 * UecSrc::_target_Qdelay; 
#define DEBUG_FLOWID (-1)

#define INIT_PULL 10000000  // needs to be large enough we don't map
                            // negative pull targets (where
                            // credit_spec > backlog) to less than
                            // zero and suffer underflow.  Real
                            // implementations will properly handle
                            // modular wrapping.

/*
scaling_factor_a = current_BDP/100Gbps*net_base_rtt //12us scaling_factor_b = 12/target_Qdelay
beta = 5*scaling_factor_a
gamma = 0.15* scaling_factor_a
alpha = 4.0* scaling_factor_a*scaling_factor_b/base_rtt gamma_g = 0.8
*/

////////////////////////////////////////////////////////////////
//  UEC NIC
////////////////////////////////////////////////////////////////

UecNIC::UecNIC(id_t src_num, EventList& eventList, linkspeed_bps linkspeed, uint32_t ports)
    : EventSource(eventList, "uecNIC"), NIC(src_num)  {
    _nodename = "uecNIC" + to_string(_src_num);
    _linkspeed = linkspeed;
    _no_of_ports = ports;
    _ports.resize(_no_of_ports);
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        _ports[p].send_end_time = 0;
        _ports[p].last_pktsize = 0;
        _ports[p].busy = false;
    }
    _busy_ports = 0;
    _rr_port = rand()%_no_of_ports; // start on a random port
    _num_queued_srcs = 0;
    _ratio_data = 1;
    _ratio_control = 10;
    _crt = 0;
}

// srcs call request_sending to see if they can send now.  If the
// answer is no, they'll be called back when it's time to send.
const Route* UecNIC::requestSending(UecSrc& src) {
    if (UecSrc::_debug) {
        cout << src.nodename() << " requestSending at "
             << timeAsUs(EventList::getTheEventList().now()) << endl;
    }
    if (_busy_ports == _no_of_ports) {
        // we're already sending on all ports
        /*
        if (_num_queued_srcs == 0 && _control.empty()) {
            // need to schedule the callback
            eventlist().sourceIsPending(*this, _send_end_time);
        }
        */
        _num_queued_srcs += 1;
        _active_srcs.push_back(&src);
        return NULL;
    }
    assert(/*_num_queued_srcs == 0 &&*/ _control.empty());
    uint32_t portnum = findFreePort();
    return src.getPortRoute(portnum);
}

uint32_t UecNIC::findFreePort() {
    assert(_busy_ports < _no_of_ports);
    do {
        _rr_port = (_rr_port + 1) % _no_of_ports;

    } while (_ports[_rr_port].busy);
    return _rr_port;
}

uint32_t UecNIC::sendOnFreePortNow(simtime_picosec endtime, const Route* rt) {
    if (rt) {
        assert(_ports[_rr_port].busy == false);
    } else {
        _rr_port = findFreePort();
    }
    _ports[_rr_port].send_end_time = endtime;
    _ports[_rr_port].busy = true;
    _busy_ports++;
    eventlist().sourceIsPending(*this, endtime);
    return _rr_port;
}

// srcs call startSending when they are allowed to actually send
void UecNIC::startSending(UecSrc& src, mem_b pkt_size, const Route* rt) {
    if (UecSrc::_debug) {
        cout << src.nodename() << " startSending at "
             << timeAsUs(EventList::getTheEventList().now()) << endl;
    }

    
    if (_num_queued_srcs > 0) {
        UecSrc* queued_src = _active_srcs.front();
        _active_srcs.pop_front();
        _num_queued_srcs--;
        assert(_num_queued_srcs >= 0);
        assert(queued_src == &src);
    }

    simtime_picosec endtime = eventlist().now() + (pkt_size * 8 * timeFromSec(1.0)) / _linkspeed;
    sendOnFreePortNow(endtime, rt);
}

// srcs call cantSend when they previously requested to send, and now its their turn, they can't for
// some reason.
void UecNIC::cantSend(UecSrc& src) {
    if (UecSrc::_debug) {
        cout << src.nodename() << " cantSend at " << timeAsUs(EventList::getTheEventList().now())
             << endl;
    }

    if (_num_queued_srcs == 0 && _control.empty()) {
        // it was an immediate send, so nothing to do if we can't send after all
        return;
    }
    if (_num_queued_srcs > 0) {
        _num_queued_srcs--;

        UecSrc* queued_src = _active_srcs.front();
        _active_srcs.pop_front();

        assert(queued_src == &src);
        assert(_busy_ports < _no_of_ports);

        if (_num_queued_srcs > 0) {
            // give the next src a chance.
            queued_src = _active_srcs.front();
            const Route* route = queued_src->getPortRoute(findFreePort());
            queued_src->timeToSend(*route);
            return;
        }
    }
    if (!_control.empty()) {
        // need to send a control packet, since we didn't manage to send a data packet.
        sendControlPktNow();
    }
}

void UecNIC::sendControlPacket(UecBasePacket* pkt, UecSrc* src, UecSink* sink) {
    assert((src || sink) && !(src && sink));
    
    _control_size += pkt->size();
    CtrlPacket cp = {pkt, src, sink};
    _control.push_back(cp);

    if (UecSrc::_debug) {
        cout << "NIC " << this << " request to send control packet of type " << pkt->str()
             << " control queue size " << _control_size << " " << _control.size() << endl;
    }

    if (_busy_ports == _no_of_ports) {
        // all ports are busy
        if (UecSrc::_debug) {
            cout << "NIC sendControlPacket " << this << " already sending on all ports\n";
        }
    } else {
        // send now!
        sendControlPktNow();
    }
}

// actually do the send of a queued control packet
void UecNIC::sendControlPktNow() {
    assert(!_control.empty());
    assert(_busy_ports != _no_of_ports);
    
    CtrlPacket cp = _control.front();
    _control.pop_front();
    UecBasePacket* p = cp.pkt;

    simtime_picosec endtime  = eventlist().now() + (p->size() * 8 * timeFromSec(1.0)) / _linkspeed;
    uint32_t port_to_use = sendOnFreePortNow(endtime, NULL);
    if (UecSrc::_debug)
        cout << "NIC " << this << " send control of size " << p->size() << " at "
             << timeAsUs(eventlist().now()) << endl;

    _control_size -= p->size();
    assert(p->route() == NULL);
    const Route* route;
    if (cp.src)
        route = cp.src->getPortRoute(port_to_use);
    else
        route = cp.sink->getPortRoute(port_to_use);
    p->set_route(*route);
    p->sendOn();
}


void UecNIC::doNextEvent() {
    // doNextEvent should be called every time a packet will have finished being sent
    uint32_t last_port = _no_of_ports;
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        if (_ports[p].busy && _ports[p].send_end_time == eventlist().now()) {
            last_port = p;
            break;
        }
    }
    assert(last_port != _no_of_ports);
    _busy_ports--;
    _ports[last_port].busy = false;

    if (UecSrc::_debug)
        cout << "NIC " << this << " doNextEvent at " << timeAsUs(eventlist().now()) << endl;

    if (_num_queued_srcs > 0 && !_control.empty()) {
        _crt++;

        if (_crt >= (_ratio_control + _ratio_data))
            _crt = 0;

        if (UecSrc::_debug) {
            cout << "NIC " << this << " round robin time between srcs " << _num_queued_srcs
                 << " and control " << _control.size() << " " << _crt;
        }

        if (_crt < _ratio_data) {
            // it's time for the next source to send
            UecSrc* queued_src = _active_srcs.front();
            const Route* route = queued_src->getPortRoute(findFreePort());
            queued_src->timeToSend(*route);

            if (UecSrc::_debug)
                cout << " send data " << endl;

            return;
        } else {
            sendControlPktNow();
            return;
        }
    }

    if (_num_queued_srcs > 0) {
        UecSrc* queued_src = _active_srcs.front();
        const Route* route = queued_src->getPortRoute(findFreePort());
        queued_src->timeToSend(*route);

        if (UecSrc::_debug)
            cout << "NIC " << this << " send data ONLY " << endl;
    } else if (!_control.empty()) {
        sendControlPktNow();
    }
}



////////////////////////////////////////////////////////////////
//  UEC SRC PORT
////////////////////////////////////////////////////////////////
UecSrcPort::UecSrcPort(UecSrc& src, uint32_t port_num)
    : _src(src), _port_num(port_num) {
}

void UecSrcPort::setRoute(const Route& route) {
    _route = &route;
}

void UecSrcPort::receivePacket(Packet& pkt) {
    _src.receivePacket(pkt, _port_num);
}

const string& UecSrcPort::nodename() {
    return _src.nodename();
}

////////////////////////////////////////////////////////////////
//  UEC SRC
////////////////////////////////////////////////////////////////

UecSrc::UecSrc(TrafficLogger* trafficLogger, EventList& eventList, UecNIC& nic, uint32_t no_of_ports, bool rts)
    : EventSource(eventList, "uecSrc"), _nic(nic), _flow(trafficLogger) {
    _node_num = _global_node_count++;
    _nodename = "uecSrc " + to_string(_node_num);

    _no_of_ports = no_of_ports;
    _ports.resize(no_of_ports);
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        _ports[p] = new UecSrcPort(*this, p);
    }
        
    _rtx_timeout_pending = false;
    _rtx_timeout = timeInf;
    _rto_timer_handle = eventlist().nullHandle();

    _flow_logger = NULL;

    _rtt = _min_rto;

    _mdev = 0;
    _rto = _min_rto;
    _logger = NULL;

    _maxwnd = 50 * _mtu;
    _cwnd = _maxwnd;
    _flow_size = 0;
    _done_sending = false;
    _backlog = 0;
    _rtx_backlog = 0;
    _pull_target = INIT_PULL;
    _pull = INIT_PULL;
    _credit = _maxwnd;
    _speculating = true;
    _in_flight = 0;
    _highest_sent = 0;
    _send_blocked_on_nic = false;
    _no_of_paths = _path_entropy_size;
    _path_random = rand() % 0xffff;  // random upper bits of EV
    _path_xor = rand() % _no_of_paths;
    _current_ev_index = 0;
    _max_penalty = 1;
    _last_rts = 0;

    // stats for debugging
    _new_packets_sent = 0;
    _rtx_packets_sent = 0;
    _rts_packets_sent = 0;
    _bounces_received = 0;

    // reset path penalties
    _ev_skip_bitmap.resize(_no_of_paths);
    for (uint32_t i = 0; i < _no_of_paths; i++) {
        _ev_skip_bitmap[i] = 0;
    }

    // by default, end silently
    _end_trigger = 0;

    _dstaddr = UINT32_MAX;
    //_route = NULL;
    _mtu = Packet::data_packet_size();
    _mss = _mtu - _hdr_size;

    _debug_src = UecSrc::_debug;
    _bdp = 0;
    _base_rtt = 0;

    _fi_count = 0;

    if (_sender_based_cc) {
        switch (_sender_cc_algo) {
            case DCTCP:
                updateCwndOnAck = &UecSrc::updateCwndOnAck_DCTCP;
                updateCwndOnNack = &UecSrc::updateCwndOnNack_DCTCP;
                break;
            case NSCC:
                updateCwndOnAck = &UecSrc::updateCwndOnAck_NSCC;
                updateCwndOnNack = &UecSrc::updateCwndOnNack_NSCC;
                break;
            default:
                cout << "Unknown CC algo specified " << _sender_cc_algo << endl;
                assert(0);
        }
    }
    if (_node_num == 2) _debug_src = true; // use this to enable debugging on one flow at a
    // time
    _received_bytes = 0;
    _recvd_bytes = 0;
}

void UecSrc::connectPort(uint32_t port_num,
                          Route& routeout,
                          Route& routeback,
                          UecSink& sink,
                          simtime_picosec start_time) {
    _ports[port_num]->setRoute(routeout);
    //_route = &routeout;

    if (port_num == 0) {
        _sink = &sink;
        //_flow.set_id(get_id());  // identify the packet flow with the UEC source that generated it
        _flow._name = _name;

        if (start_time != TRIGGER_START) {
            eventlist().sourceIsPending(*this, timeFromUs((uint32_t)start_time));
        }
    }
    assert(_sink == &sink);
    _sink->connectPort(port_num, *this, routeback);
}

void UecSrc::receivePacket(Packet& pkt, uint32_t portnum) {
    switch (pkt.type()) {
        case UECDATA: {
            _bounces_received++;
            // TBD - this is likely a Back-to-sender packet
            abort();
        }
        case UECRTS: {
            abort();
        }
        case UECACK: {
            processAck((const UecAckPacket&)pkt);
            pkt.free();
            return;
        }
        case UECNACK: {
            processNack((const UecNackPacket&)pkt);
            pkt.free();
            return;
        }
        case UECPULL: {
            processPull((const UecPullPacket&)pkt);
            pkt.free();
            return;
        }
        default: {
            abort();
        }
    }
}

mem_b UecSrc::handleAckno(UecDataPacket::seq_t ackno) {
    auto i = _tx_bitmap.find(ackno);
    if (i == _tx_bitmap.end())
        return 0;
    // mem_b pkt_size = i->second.pkt_size;
    simtime_picosec send_time = i->second.send_time;

    mem_b pkt_size = i->second.pkt_size;
    
    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " handleAck " << ackno << " flow " << _flow.str() << endl;
    _tx_bitmap.erase(i);
    _send_times.erase(send_time);

    if (send_time == _rto_send_time) {
        recalculateRTO();
    }

    return pkt_size;
}

mem_b UecSrc::handleCumulativeAck(UecDataPacket::seq_t cum_ack) {
    mem_b newly_acked = 0;

    // free up anything cumulatively acked
    while (!_rtx_queue.empty()) {
        auto seqno = _rtx_queue.begin()->first;

        if (seqno < cum_ack) {
            _rtx_queue.erase(_rtx_queue.begin());
        } else
            break;
    }

    auto i = _tx_bitmap.begin();
    while (i != _tx_bitmap.end()) {
        auto seqno = i->first;
        // cumulative ack is next expected packet, not yet received
        if (seqno >= cum_ack) {
            // nothing else acked
            break;
        }
        simtime_picosec send_time = i->second.send_time;

        newly_acked += i->second.pkt_size;

        if (_debug_src)
            cout << _flow.str() << " " << _nodename << " handleCumAck " << seqno << " flow " << _flow.str() << endl;

        _tx_bitmap.erase(i);
        i = _tx_bitmap.begin();
        _send_times.erase(send_time);
        if (send_time == _rto_send_time) {
            recalculateRTO();
        }
    }
    return newly_acked;
}

void UecSrc::handlePull(UecBasePacket::pull_quanta pullno) {
    if (pullno > _pull) {
        UecBasePacket::pull_quanta extra_credit = pullno - _pull;
        _credit += UecBasePacket::unquantize(extra_credit);
        if (_credit > _maxwnd)
            _credit = _maxwnd;
        _pull = pullno;
    }
}

bool UecSrc::checkFinished(UecDataPacket::seq_t cum_ack) {
    // cum_ack gives the next expected packet
    if (_done_sending) {
        // if (UecSrc::_debug) cout << _nodename << " checkFinished done sending " << " cum_acc "
        // << cum_ack << " mss " << _mss << " c*m " << cum_ack * _mss << endl;
        return true;
    }
    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " checkFinished "
             << " cum_acc " << cum_ack << " mss " << _mss << " RTS sent " << _rts_packets_sent
             << " total bytes " << ((int64_t)cum_ack - _rts_packets_sent) * _mss << " flow_size "
             << _flow_size << " done_sending " << _done_sending << endl;

    if ((((mem_b)cum_ack - _rts_packets_sent) * _mss) >= _flow_size) {
        cout << "Flow " << _name << " flowId " << flowId() << " " << _nodename << " finished at "
             << timeAsUs(eventlist().now()) << " total packets " << cum_ack << " RTS "
             << _rts_packets_sent << " total bytes " << ((mem_b)cum_ack - _rts_packets_sent) * _mss
             << endl;
        _speculating = false;
        if (_end_trigger) {
            _end_trigger->activate();
        }
        if (_flow_logger) {
            _flow_logger->logEvent(_flow, *this, FlowEventLogger::FINISH, _flow_size, cum_ack);
        }
        _done_sending = true;
        return true;
    }
    return false;
}

void UecSrc::processAck(const UecAckPacket& pkt) {
    _nic.logReceivedCtrl(pkt.size());
    
    auto cum_ack = pkt.cumulative_ack();

    //handle flight_size based on recvd_bytes in packet.
    uint64_t newly_recvd_bytes = 0;

    if (pkt.recvd_bytes() > _recvd_bytes){
        newly_recvd_bytes = pkt.recvd_bytes() - _recvd_bytes;
        _recvd_bytes = pkt.recvd_bytes();

        _achieved_bytes += newly_recvd_bytes;
        _received_bytes += newly_recvd_bytes;
        _bytes_ignored += newly_recvd_bytes;
    }

    //decrease flightsize.
    _in_flight -= newly_recvd_bytes;
    assert(_in_flight >= 0);

    if (_sender_based_cc && pkt.rcv_wnd_pen() < 255) {
            sint64_t window_decrease = newly_recvd_bytes - newly_recvd_bytes * pkt.rcv_wnd_pen() / 255;
            _cwnd = max(_cwnd-window_decrease, (mem_b)_mtu);
    }

    //compute RTT sample
    auto acked_psn = pkt.acked_psn();
    auto i = _tx_bitmap.find(acked_psn);

    mem_b pkt_size;
    simtime_picosec delay;

    if (i != _tx_bitmap.end()) {
        //auto seqno = i->first;
        simtime_picosec send_time = i->second.send_time;
        pkt_size = i->second.pkt_size;

        _raw_rtt = eventlist().now() - send_time;
        update_delay(_raw_rtt, true);
        delay = _raw_rtt - _base_rtt;     
        cout << " send_time " << timeAsUs(send_time) << " now " << timeAsUs(eventlist().now()) << " sample " << timeAsUs(_raw_rtt) << endl;
    } else {
        // this can happen when the ACK arrives later than a cumulative ACK covering the NACKed
        // packet.
        cout << "Can't find send record for seqno " << acked_psn << endl;
        pkt_size = _mtu;
        delay = get_avg_delay();
    }

    handleCumulativeAck(cum_ack);

    if (_debug_src)
        cout << "At " << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << _nodename << " processAck cum_ack: " << cum_ack << " flow " << _flow.str() << endl;

    auto ackno = pkt.ref_ack();

    uint64_t bitmap = pkt.bitmap();

    if (_debug_src)
        cout << "    ref_ack: " << ackno << " bitmap: " << bitmap << endl;

    while (bitmap > 0) {
        if (bitmap & 1) {
            if (_debug_src)
                cout << "    Sack " << ackno << " flow " << _flow.str() << endl;

            handleAckno(ackno);
        }
        ackno++;
        bitmap >>= 1;
    }



    // handle ECN echo only if the path is bad for now. Will update to match full spec afterwards. 
    if (pkt.ecn_echo()) {
        penalizePath(pkt.ev(), 1);
    }

    average_ecn_bytes(pkt_size,newly_recvd_bytes, pkt.ecn_echo());
    if(_flow.flow_id() == DEBUG_FLOWID  ){
        cout <<  timeAsUs(eventlist().now()) << " flowid " << _flow.flow_id() << " track_avg_rtt " << timeAsUs(get_avg_delay())
            << " rtt " << timeAsUs(_raw_rtt) << " skip " << pkt.ecn_echo()
            << endl;
    }
    if (_sender_based_cc){
        (this->*updateCwndOnAck)(pkt.ecn_echo(), delay, newly_recvd_bytes);
    }

    cout << "At " << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << _nodename << " processAck: " << cum_ack << " flow " << _flow.str() << " cwnd " << _cwnd << " flightsize " << _in_flight << " delay " << timeAsUs(delay) << " newlyrecvd " << newly_recvd_bytes << " skip " << pkt.ecn_echo() << " raw rtt " << _raw_rtt <<  " ecn avg " << _exp_avg_ecn << endl;

    stopSpeculating();

    if (checkFinished(cum_ack)) {
        return;
    }

    sendIfPermitted();
}

void UecSrc::updateCwndOnAck_DCTCP(bool skip, simtime_picosec rtt, mem_b newly_acked_bytes) {
    cout << timeAsUs(eventlist().now()) << " DCTCP start " << _name << " cwnd " << _cwnd
         << " with params skip " << skip << " acked bytes " << newly_acked_bytes << endl;

    if (skip == false)  // additive increase, 1 PKT /RTT
    {
        _cwnd += newly_acked_bytes * _mtu / _cwnd;

    } else {  // multiplicative decrease, done per mark, more aggressive than DCTCP (less smoothing)
              // but much simpler and more responsive since we don't need to track alpha.
        _cwnd -= newly_acked_bytes / 3;
        _cwnd = max((mem_b)_mtu, _cwnd);
    }
}

void UecSrc::updateCwndOnNack_DCTCP(bool skip, simtime_picosec rtt, mem_b nacked_bytes) {
    _cwnd -= nacked_bytes;
    _cwnd = max(_cwnd, (mem_b)_mtu);
}

bool UecSrc::quick_adapt(bool is_loss, simtime_picosec avgqdelay) {
    if (_receiver_based_cc)
        return false;

    cout << "At " << timeAsUs(eventlist().now()) << " " << _flow.str() << " quickadapt called is loss "<< is_loss << " delay " << avgqdelay << " qa_endtime " << timeAsUs(_qa_endtime) << " trigger qa " << _trigger_qa << endl;
    if (eventlist().now()>_qa_endtime){
        if (_qa_endtime != 0 && (_trigger_qa || is_loss || (avgqdelay > _qa_threshold))){
                cout << "At " << timeAsUs(eventlist().now()) << " " << _flow.str() << " running quickadapt, CWND is " << _cwnd << " setting it to " << _achieved_bytes <<  endl;

                if (_cwnd < _achieved_bytes){
                    cout << "This shouldn't happen: QUICK ADAPT MIGHT INCREASE THE CWND" << endl;
                }
                else {
                    _cwnd = max(_achieved_bytes, (uint64_t)_mtu) * _qa_scaling;
                    if(_flow.flow_id() == DEBUG_FLOWID)
                        cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " quick_adapt  _cwnd " << _cwnd << endl;
                    _bytes_to_ignore = _in_flight;
                    _bytes_ignored = 0;
                    _trigger_qa = false;
                    return true;
                }
        }
        _achieved_bytes = 0;
        _qa_endtime = eventlist().now() + _base_rtt;
    }
    return false;
}

void UecSrc::fair_increase(uint32_t newly_acked_bytes){
    _inc_bytes += _fi * _mtu * newly_acked_bytes; //increase by 16Million!
}

void UecSrc::proportional_increase(uint32_t newly_acked_bytes,simtime_picosec delay){
    fast_increase(newly_acked_bytes, delay);
    if (_increase)
        return;
    
    //make sure targetQdelay > delay;
    assert(_target_Qdelay > delay);

    _inc_bytes += min((double)newly_acked_bytes * _pi, _alpha * (_target_Qdelay - delay));

    fair_increase(newly_acked_bytes);
}

void UecSrc::fast_increase(uint32_t newly_acked_bytes,simtime_picosec delay){
    if (delay < timeFromUs(1u)){
        _fi_count += newly_acked_bytes;
        if (_fi_count > _cwnd || _increase){
            _cwnd += newly_acked_bytes * _fi_scale;

            if (_cwnd > _maxwnd)
                _cwnd = _maxwnd;

            _increase = true;
            return;
        }
    }
    else _fi_count = 0;

    _increase = false;
}

void UecSrc::fair_decrease(bool can_decrease, uint32_t newly_acked_bytes){
    _increase = false;
    _fi_count = 0;
    if (can_decrease)
        _dec_bytes += _fd * newly_acked_bytes;
}

void UecSrc::multiplicative_decrease(bool can_decrease, uint32_t newly_acked_bytes){
    _increase = false;
    _fi_count = 0;
    simtime_picosec avg_delay = get_avg_delay();
    if (can_decrease && avg_delay > _target_Qdelay){
        if (eventlist().now() - _last_dec_time > _base_rtt){
            _cwnd *= max(1-_gamma*(avg_delay-_target_Qdelay)/avg_delay, 0.5);/*_max_md_jump instead of 1*/
            _last_dec_time = eventlist().now();
        }
        fair_decrease(can_decrease, newly_acked_bytes);
    }
}

void UecSrc::fulfill_adjustment(){
    cout << "Running fulfill adjustment cwnd " << _cwnd << " inc " << _inc_bytes << " dec " << _dec_bytes << " bdp " << _bdp << endl;
    _cwnd += (_inc_bytes)/_cwnd - (_dec_bytes * _cwnd / _bdp);

    if (_cwnd < _mtu)
        _cwnd = _mtu;

    if (_cwnd > _maxwnd)
        _cwnd = _maxwnd;

    _inc_bytes = 0;
    _dec_bytes = 0;

    //unclear what received_bytes this is referring to. 
    _received_bytes = 0;

    sendIfPermitted();
}

void UecSrc::mark_packet_for_retransmission(UecBasePacket::seq_t psn, uint16_t pktsize){
    _in_flight -= pktsize;
    assert (_in_flight>=0);
    _cwnd = max(_cwnd - pktsize, (mem_b)_mtu);

    //_rtx_count ++;
}

void UecSrc::updateCwndOnAck_NSCC(bool skip, simtime_picosec delay, mem_b newly_acked_bytes) {
    bool can_decrease = _exp_avg_ecn > _ecn_thresh;

    if (_bytes_ignored < _bytes_to_ignore && skip)
        return;

    simtime_picosec avg_delay = get_avg_delay();

    if (quick_adapt(false,avg_delay))
        return;

    if (!skip && delay >= _target_Qdelay){
        fair_increase(newly_acked_bytes);
        if(_flow.flow_id() == DEBUG_FLOWID)
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " fair_increase  _smarttrack_cwnd " << _cwnd << endl;
    }else if (!skip && delay < _target_Qdelay){
        proportional_increase(newly_acked_bytes,delay);
        if(_flow.flow_id() == DEBUG_FLOWID)
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " proportional_increase _smarttrack_cwnd " << _cwnd << endl;        
    }
    else if (skip && delay >= _target_Qdelay){    
        multiplicative_decrease(can_decrease,newly_acked_bytes);
        if(_flow.flow_id() == DEBUG_FLOWID){
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " multiplicative_decrease _smarttrack_cwnd " << _cwnd << endl;
        }
    }else if (skip && delay < _target_Qdelay){
        fair_decrease(can_decrease,newly_acked_bytes);
        if(_flow.flow_id() == DEBUG_FLOWID)
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " fair_decrease _smarttrack_cwnd " << _cwnd << endl;        
    }

    if ( _received_bytes > _adjust_bytes_threshold){
        fulfill_adjustment();
        if(_flow.flow_id() == DEBUG_FLOWID)
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " fulfill_adjustment _smarttrack_cwnd " << _cwnd << endl;
    }

    if (eventlist().now() - _last_adjust_time > _adjust_period_threshold ){
        _cwnd += _eta;
        _last_adjust_time = eventlist().now();
        if(_flow.flow_id() == DEBUG_FLOWID)
            cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " eta _smarttrack_cwnd " << _cwnd << endl;
    }

    if(_flow.flow_id() == DEBUG_FLOWID)
        cout <<timeAsUs(eventlist().now()) <<" flowid " << _flow.flow_id()<< " final _smarttrack_cwnd " << _cwnd << endl;
}

void UecSrc::updateCwndOnNack_NSCC(bool skip, simtime_picosec rtt, mem_b nacked_bytes) {
    _trigger_qa = true;
    if (_bytes_ignored >= _bytes_to_ignore)
        quick_adapt(true, get_avg_delay());
}

void UecSrc::update_delay(simtime_picosec raw_rtt, bool update_avg){
    bool new_base_rtt = false;

    if (_base_rtt == 0 || _base_rtt > _raw_rtt){
        _base_rtt = _raw_rtt;
        new_base_rtt = true;
    }

    if (_bdp == 0 || new_base_rtt){
        //reinitialize BDP, we have new RTT sample.
        _bdp = _raw_rtt * _nic.linkspeed() / 8000000000000;
        _maxwnd = 2 * _bdp;
    }

    simtime_picosec delay = _raw_rtt - _base_rtt;
    if(update_avg){
        _avg_delay = _delay_alpha * delay + (1-_delay_alpha) * _avg_delay;
    }
    cout << "Update delay with sample " << timeAsUs(delay) << " avg is " << timeAsUs(_avg_delay) << " base rtt is " << _base_rtt << endl;
}

simtime_picosec UecSrc::get_avg_delay(){
    return _avg_delay;
}

void UecSrc::average_ecn_bytes(uint32_t pktsize, uint32_t newly_acked_bytes, bool skip) {
    double value = (double)skip * pktsize / newly_acked_bytes;
    _exp_avg_ecn = _ecn_alpha * value + (1 - _ecn_alpha) * _exp_avg_ecn;
}

void UecSrc::processNack(const UecNackPacket& pkt) {
    _nic.logReceivedCtrl(pkt.size());

    // auto pullno = pkt.pullno();
    // handlePull(pullno);

    auto nacked_seqno = pkt.ref_ack();
//    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " processNack nacked: " << nacked_seqno << " flow " << _flow.str()
             << endl;

    uint16_t ev = pkt.ev();
    // what should we do when we get a NACK with ECN_ECHO set?  Presumably ECE is superfluous?
    // bool ecn_echo = pkt.ecn_echo();

    // move the packet to the RTX queue
    auto i = _tx_bitmap.find(nacked_seqno);
    if (i == _tx_bitmap.end()) {
        if (_debug_src)
            cout << _flow.str() << " " << "Didn't find NACKed packet in _active_packets flow " << _flow.str() << endl;

        // this abort is here because this is unlikely to happen in
        // simulation - when it does, it is usually due to a bug
        // elsewhere.  But if you discover a case where this happens
        // for real, remove the abort and uncomment the return below.
        abort();
        // this can happen when the NACK arrives later than a cumulative ACK covering the NACKed
        // packet. return;
    }

    mem_b pkt_size = i->second.pkt_size;

    assert(pkt_size >= _hdr_size);  // check we're not seeing NACKed RTS packets.
    if (pkt_size == _hdr_size) {
        _stats.rts_nacks++;
    }

    auto seqno = i->first;
    simtime_picosec send_time = i->second.send_time;

    _raw_rtt = eventlist().now() - send_time;
    update_delay(_raw_rtt, false);

    if (_sender_based_cc)
        (this->*updateCwndOnNack)(ev,_raw_rtt,pkt_size);

    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " erasing send record, seqno: " << seqno << " flow " << _flow.str()
             << endl;
    _tx_bitmap.erase(i);
    assert(_tx_bitmap.find(seqno) == _tx_bitmap.end());  // xxx remove when working

    _in_flight -= pkt_size;
    assert(_in_flight >= 0);

    _send_times.erase(send_time);

    stopSpeculating();
    queueForRtx(seqno, pkt_size);

    if (send_time == _rto_send_time) {
        recalculateRTO();
    }

    penalizePath(ev, 1);
    sendIfPermitted();
}

void UecSrc::processPull(const UecPullPacket& pkt) {
    _nic.logReceivedCtrl(pkt.size());

    auto pullno = pkt.pullno();
    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " processPull " << pullno << " flow " << _flow.str() << " SP " << pkt.is_slow_pull() << endl;

    stopSpeculating();
    handlePull(pullno);
    sendIfPermitted();
}

void UecSrc::doNextEvent() {
    // a timer event fired.  Can either be a timeout, or the timed start of the flow.
    if (_rtx_timeout_pending) {
        clearRTO();
        assert(_logger == 0);

        if (_logger)
            _logger->logUec(*this, UecLogger::UEC_TIMEOUT);

        rtxTimerExpired();
    } else {
        if (_debug_src)
            cout << _flow.str() << " " << "Starting flow " << _name << endl;
        startFlow();
    }
}

void UecSrc::setFlowsize(uint64_t flow_size_in_bytes) {
    _flow_size = flow_size_in_bytes;
}

void UecSrc::startFlow() {
    //_cwnd = _maxwnd;
    _credit = _maxwnd;
    if (_debug_src)
        cout << _flow.str() << " " << "startflow " << _flow._name << " CWND " << _cwnd << " at "
             << timeAsUs(eventlist().now()) << " flow " << _flow.str() << endl;

    cout << "Flow " << _name << " flowId " << flowId() << " " << _nodename << " starting at "
         << timeAsUs(eventlist().now()) << endl;

    if (_flow_logger) {
        _flow_logger->logEvent(_flow, *this, FlowEventLogger::START, _flow_size, 0);
    }
    clearRTO();
    _in_flight = 0;
    _pull_target = INIT_PULL;
    _pull = INIT_PULL;
    _last_rts = 0;
    // backlog is total amount of data we expect to send, including headers
    _backlog = ceil(((double)_flow_size) / _mss) * _hdr_size + _flow_size;
    _rtx_backlog = 0;
    _send_blocked_on_nic = false;
    while (_send_blocked_on_nic == false && credit() > 0 && _backlog > 0) {
        if (_debug_src)
            cout << _flow.str() << " " << "requestSending 0 "<< endl;

        const Route *route = _nic.requestSending(*this);
        if (route) {
            // if we're here, there's no NIC queue
            mem_b sent_bytes = sendNewPacket(*route);
            if (sent_bytes > 0) {
                _nic.startSending(*this, sent_bytes, route);
            } else {
                _nic.cantSend(*this);
            }
        } else {
            _send_blocked_on_nic = true;
            return;
        }
    }
}

mem_b UecSrc::credit() const {
    return _credit;
}

void UecSrc::spendCredit(mem_b pktsize) {
    if (_receiver_based_cc){
        assert(_credit > 0);
        _credit -= pktsize;
    }
}

void UecSrc::stopSpeculating() {
    // this doesn't really do a lot, except prevent us retransmitting
    // on an RTO before we've heard back from the receiver
    if (_speculating) {
        _speculating = false;
        if (_credit > 0)
            _credit = 0;
    }
}

UecBasePacket::pull_quanta UecSrc::computePullTarget() {
    if (!_receiver_based_cc)
        return 0;

    mem_b pull_target = _backlog + _rtx_backlog;
    //mem_b pull_target = _backlog;

    if (_sender_based_cc) {
        if (pull_target > _cwnd + _mtu) {
            pull_target = _cwnd + _mtu;
        }
    }

    if (pull_target > _maxwnd) {
        pull_target = _maxwnd;
    }

    pull_target -= _credit;
    pull_target += UecBasePacket::unquantize(_pull);

    UecBasePacket::pull_quanta quant_pull_target = UecBasePacket::quantize_ceil(pull_target);

    if (_debug_src)
        cout << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << " " << nodename()
             << " pull_target: " << UecBasePacket::unquantize(quant_pull_target) << " beforequant "
             << pull_target << " pull " << UecBasePacket::unquantize(_pull) << " diff "
             << UecBasePacket::unquantize(quant_pull_target - _pull) << " credit "
             << _credit
             << " backlog " << _backlog << " rtx_backlog " << _rtx_backlog << " active sources "
             << _nic.activeSources() << endl;
    return quant_pull_target;
}

void UecSrc::sendIfPermitted() {
    // send if the NIC, credit and window allow.                                                                                                                  

    if (_receiver_based_cc && credit() <= 0) {
        // can send if we have *any* credit, but we don't                                                                                                         
        return;
    }

    //cout << timeAsUs(eventlist().now()) << " " << nodename() << " FOO " << _cwnd << " " << _in_flight << endl;                                                  
    if (_sender_based_cc) {
        if (_cwnd <= _in_flight) {
            return;
        }
    }

    if (_rtx_queue.empty()) {
        if (_backlog == 0) {
            return;
        }
    }

    if (_send_blocked_on_nic) {
        // the NIC already knows we want to send                                                                                                                  
        return;
    }

    // we can send if the NIC lets us.                                                                                                                            
    if (_debug_src)
        cout << _flow.str() << " " << "requestSending 1\n";

    const Route* route = _nic.requestSending(*this);
    if (route) {
        mem_b sent_bytes = sendPacket(*route);
        if (sent_bytes > 0) {
            _nic.startSending(*this, sent_bytes, route);
        } else {
            _nic.cantSend(*this);
        }
    } else {
        // we can't send yet, but NIC will call us back when we can                                                                                               
        _send_blocked_on_nic = true;
        return;
    }
}


// if sendPacket got called, we have already asked the NIC for
// permission, and we've already got both credit and cwnd to send, so
// we will likely be sending something (sendNewPacket can return 0 if
// we only had speculative credit we're not allowed to use though)
mem_b UecSrc::sendPacket(const Route& route) {
    if (_rtx_queue.empty()) {
        return sendNewPacket(route);
    } else {
        return sendRtxPacket(route);
    }
}

void UecSrc::startRTO(simtime_picosec send_time) {
    if (!_rtx_timeout_pending) {
        // timer is not running - start it
        _rtx_timeout_pending = true;
        _rtx_timeout = send_time + _rto;
        _rto_send_time = send_time;

        if (_rtx_timeout < eventlist().now())
            _rtx_timeout = eventlist().now();

        if (_debug)
            cout << "Start timer at " << timeAsUs(eventlist().now()) << " source " << _flow.str()
                 << " expires at " << timeAsUs(_rtx_timeout) << " flow " << _flow.str() << endl;

        _rto_timer_handle = eventlist().sourceIsPendingGetHandle(*this, _rtx_timeout);
        if (_rto_timer_handle == eventlist().nullHandle()) {
            // this happens when _rtx_timeout is past the configured simulation end time.
            _rtx_timeout_pending = false;
            if (_debug)
                cout << "Cancel timer because too late for flow " << _flow.str() << endl;
        }
    } else {
        // timer is already running
        if (send_time + _rto < _rtx_timeout) {
            // RTO needs to expire earlier than it is currently set
            cancelRTO();
            startRTO(send_time);
        }
    }
}

void UecSrc::clearRTO() {
    // clear the state
    _rto_timer_handle = eventlist().nullHandle();
    _rtx_timeout_pending = false;

    if (_debug)
        cout << "Clear RTO " << timeAsUs(eventlist().now()) << " source " << _flow.str() << endl;
}

void UecSrc::cancelRTO() {
    if (_rtx_timeout_pending) {
        // cancel the timer
        eventlist().cancelPendingSourceByHandle(*this, _rto_timer_handle);
        clearRTO();
    }
}

void UecSrc::penalizePath(uint16_t path_id, uint8_t penalty) {
    // _no_of_paths must be a power of 2
    uint16_t mask = _no_of_paths - 1;
    path_id &= mask;  // only take the relevant bits for an index
    _ev_skip_bitmap[path_id] += penalty;
    if (_ev_skip_bitmap[path_id] > _max_penalty) {
        _ev_skip_bitmap[path_id] = _max_penalty;
    }
}

uint16_t UecSrc::nextEntropy() {
    // _no_of_paths must be a power of 2
    uint16_t mask = _no_of_paths - 1;
    uint16_t entropy = (_current_ev_index ^ _path_xor) & mask;
    while (_ev_skip_bitmap[entropy] > 0) {
        _ev_skip_bitmap[entropy]--;
        _current_ev_index++;
        if (_current_ev_index == _no_of_paths) {
            _current_ev_index = 0;
            _path_xor = rand() & mask;
        }
        entropy = (_current_ev_index ^ _path_xor) & mask;
    }

    // set things for next time
    _current_ev_index++;
    if (_current_ev_index == _no_of_paths) {
        _current_ev_index = 0;
        _path_xor = rand() & mask;
    }

    entropy |= _path_random ^ (_path_random & mask);  // set upper bits
    return entropy;
}

mem_b UecSrc::sendNewPacket(const Route& route) {
    if (_debug_src)
        cout << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << _nodename
             << " sendNewPacket highest_sent " << _highest_sent << " h*m "
             << _highest_sent * _mss << " backlog " << _backlog << " flow "
             << _flow.str() << endl;
    assert(_backlog > 0);
    assert(((mem_b)_highest_sent - _rts_packets_sent) * _mss < _flow_size);
    mem_b full_pkt_size = _mtu;
    if (_backlog < _mtu) {
        full_pkt_size = _backlog;
    }

    // check we're allowed to send according to state machine
    if (_receiver_based_cc)
        assert(credit() > 0);
        
    spendCredit(full_pkt_size);

    _backlog -= full_pkt_size;
    assert(_backlog >= 0);
    _in_flight += full_pkt_size;
    auto ptype = UecDataPacket::DATA_PULL;
    if (_speculating) {
        ptype = UecDataPacket::DATA_SPEC;
    }
    _pull_target = computePullTarget();

    auto* p = UecDataPacket::newpkt(_flow, route, _highest_sent, full_pkt_size, ptype,
                                     _pull_target, _dstaddr);
    uint16_t ev = nextEntropy();
    p->set_pathid(ev);
    p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);

    if (_backlog == 0 || (_receiver_based_cc && _credit < 0) || ( _sender_based_cc &&  _in_flight >= _cwnd) ) 
        p->set_ar(true);

    createSendRecord(_highest_sent, full_pkt_size);
    if (_debug_src)
        cout << timeAsUs(eventlist().now()) << " " << _flow.str() << " sending pkt " << _highest_sent
             << " size " << full_pkt_size << " pull target " << _pull_target << " ack request " << p->ar()
             << " cwnd " << _cwnd << " in_flight " << _in_flight << endl;
    p->sendOn();
    _highest_sent++;
    _new_packets_sent++;
    startRTO(eventlist().now());
    return full_pkt_size;
}

mem_b UecSrc::sendRtxPacket(const Route& route) {
    assert(!_rtx_queue.empty());
    auto seq_no = _rtx_queue.begin()->first;
    mem_b full_pkt_size = _rtx_queue.begin()->second;
    spendCredit(full_pkt_size);

    _rtx_queue.erase(_rtx_queue.begin());
    _rtx_backlog -= full_pkt_size;
    assert(_rtx_backlog >= 0);
    _in_flight += full_pkt_size;
    _pull_target = computePullTarget();
    
    auto* p = UecDataPacket::newpkt(_flow, route, seq_no, full_pkt_size, UecDataPacket::DATA_RTX,
                                     _pull_target, _dstaddr);
    uint16_t ev = nextEntropy();
    p->set_pathid(ev);
    p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);

    createSendRecord(seq_no, full_pkt_size);

    if (_debug_src)
        cout << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << _nodename << " sending rtx pkt " << seq_no
             << " size " << full_pkt_size << " cwnd " << _cwnd
             << " in_flight " << _in_flight << " pull_target " << _pull_target << " pull " << _pull << endl;
    p->set_ar(true);
    p->sendOn();
    _rtx_packets_sent++;
    startRTO(eventlist().now());
    return full_pkt_size;
}

void UecSrc::sendRTS() {
    if (_last_rts > 0 && eventlist().now() - _last_rts < _rtt) {
        // Don't send more than one RTS per RTT, or we can create an
        // incast of RTS.  Once per RTT is enough to restart things if we lost
        // a whole window.
        return;
    }
    if (_debug_src)
        cout << timeAsUs(eventlist().now()) << " " << _flow.str() << " " << _nodename << " sendRTS, flow " << _flow.str()
             << " epsn " << _highest_sent << " last RTS " << timeAsUs(_last_rts)
             << " in_flight " << _in_flight << " pull_target " << _pull_target << " pull " << _pull << endl;
    createSendRecord(_highest_sent, _hdr_size);
    auto* p =
        UecRtsPacket::newpkt(_flow, NULL, _highest_sent, _pull_target, _dstaddr);
    p->set_dst(_dstaddr);
    uint16_t ev = nextEntropy();
    p->set_pathid(ev);

    // p->sendOn();
    _nic.sendControlPacket(p, this, NULL);

    _highest_sent++;
    _rts_packets_sent++;
    _last_rts = eventlist().now();
    startRTO(eventlist().now());
}

void UecSrc::createSendRecord(UecBasePacket::seq_t seqno, mem_b full_pkt_size) {
    // assert(full_pkt_size > 64);
    if (_debug_src)
        cout << _flow.str() << " " << _nodename << " createSendRecord seqno: " << seqno << " size " << full_pkt_size
             << endl;
    assert(_tx_bitmap.find(seqno) == _tx_bitmap.end());
    _tx_bitmap.emplace(seqno, sendRecord(full_pkt_size, eventlist().now()));
    _send_times.emplace(eventlist().now(), seqno);
}

void UecSrc::queueForRtx(UecBasePacket::seq_t seqno, mem_b pkt_size) {
    assert(_rtx_queue.find(seqno) == _rtx_queue.end());
    _rtx_queue.emplace(seqno, pkt_size);
    _rtx_backlog += pkt_size;
    if (!_speculating || !_receiver_based_cc)
        sendIfPermitted();
}

void UecSrc::timeToSend(const Route& route) {
    if (_debug_src)
        cout << "timeToSend"
             << " flow " << _flow.str() << " at " << timeAsUs(eventlist().now()) << endl;

    if (_backlog == 0 && _rtx_queue.empty()) {
        _nic.cantSend(*this);
        return;
    }
    // time_to_send is called back from the UecNIC when it's time for
    // this src to send.  To get called back, the src must have
    // previously told the NIC it is ready to send by calling
    // UecNIC::requestSending()
    //
    // before returning, UecSrc needs to call either
    // UecNIC::startSending or UecNIC::cantSend from this function
    // to update the NIC as to what happened, so they stay in sync.
    _send_blocked_on_nic = false;

    mem_b full_pkt_size = _mtu;
    // how much do we want to send?
    if (_rtx_queue.empty()) {
        assert(_rtx_backlog == 0);
        // we want to send new data
        if (_backlog < _mtu) {
            full_pkt_size = _backlog;
        }
    } else {
        // we want to retransmit
        full_pkt_size = _rtx_queue.begin()->second;
    }

    if (_sender_based_cc) {
        if (_cwnd < _in_flight) {
            if (_debug_src)
                cout << _flow.str() << " " << _node_num << "cantSend, limited by sender CWND " << _cwnd << " _in_flight "
                     << _in_flight << "\n";

            _nic.cantSend(*this);
            return;
        }
    }

    // do we have enough credit if we're using receiver CC?
    if (_receiver_based_cc && credit() <= 0) {
        if (_debug_src)
            cout << "cantSend"
                 << " flow " << _flow.str() << endl;
        ;
        _nic.cantSend(*this);
        return;
    }

    // OK, we're probably good to send
    mem_b bytes_sent = 0;
    if (_rtx_queue.empty()) {
        bytes_sent = sendNewPacket(route);
    } else {
        bytes_sent = sendRtxPacket(route);
    }

    // let the NIC know we sent, so it can calculate next send time.
    if (bytes_sent > 0) {
        _nic.startSending(*this, full_pkt_size, NULL);
    } else {
        _nic.cantSend(*this);
        return;
    }
    if (_sender_based_cc) {
        if (_cwnd < _in_flight) {
            return;
        }
    }
    // do we have enough credit to send again?
    if (_receiver_based_cc && credit() <= 0) {
        return;
    }

    if (_backlog == 0 && _rtx_queue.empty()) {
        // we're done - nothing more to send.
        return;
    }

    // we're ready to send again.  Let the NIC know.
    assert(!_send_blocked_on_nic);
    if (_debug_src)
        cout << "requestSending2"
             << " flow " << _flow.str() << endl;
    ;
    const Route* newroute = _nic.requestSending(*this);
    // we've just sent - NIC will say no, but will call us back when we can send.
    assert(!newroute);
    _send_blocked_on_nic = true;
}

void UecSrc::recalculateRTO() {
    // we're no longer waiting for the packet we set the timer for -
    // figure out what the timer should be now.
    cancelRTO();
    if (_send_times.empty()) {
        // nothing left that we're waiting for
        return;
    }
    auto earliest_send_time = _send_times.begin()->first;
    startRTO(earliest_send_time);
}

void UecSrc::rtxTimerExpired() {
    assert(eventlist().now() == _rtx_timeout);
    clearRTO();

    auto first_entry = _send_times.begin();
    assert(first_entry != _send_times.end());
    auto seqno = first_entry->second;

    auto send_record = _tx_bitmap.find(seqno);
    assert(send_record != _tx_bitmap.end());
    mem_b pkt_size = send_record->second.pkt_size;

    // update flightsize?

    _send_times.erase(first_entry);
    if (_debug_src)
        cout << _nodename << " rtx timer expired for " << seqno << " flow " << _flow.str() << endl;
    _tx_bitmap.erase(send_record);
    recalculateRTO();

    if (_sender_based_cc)
        mark_packet_for_retransmission(seqno, pkt_size);

    if (!_rtx_queue.empty()) {
        // there's already a queue, so clearly we shouldn't just
        // resend right now.  But send an RTS (no more than once per
        // RTT) to cover the case where the receiver doesn't know
        // we're waiting.
        stopSpeculating();  

        queueForRtx(seqno, pkt_size);

        if (_receiver_based_cc)
            sendRTS();

        if (_debug_src)
            cout << "sendRTS 1"
                 << " flow " << _flow.str() << endl;
        ;

        return;
    }

    // there's no queue, so maybe we could just resend now?
    queueForRtx(seqno, pkt_size);

    if (_sender_based_cc) {
        if (_cwnd < pkt_size + _in_flight) {
            // window won't allow us to send yet.
            sendRTS();
            return;
        }
    }

    if (_receiver_based_cc && _credit <= 0) {
        // we don't have any credit to send.  Send an RTS (no more than once per RTT)
        // to cover the case where the receiver doesn't know to send
        // us credit
        if (_debug_src)
            cout << "sendRTS 2"
                 << " flow " << _flow.str() << endl;

        sendRTS();
        return;
    }

    // we've got enough pulled credit or window already to send this, so see if the NIC
    // is ready right now
    if (_debug_src)
        cout << "requestSending 4\n"
             << " flow " << _flow.str() << endl;

    const Route* route = _nic.requestSending(*this);
    if (route) {
        bool bytes_sent = sendRtxPacket(*route);
        if (bytes_sent > 0) {
            _nic.startSending(*this, bytes_sent, route);
        } else {
            _nic.cantSend(*this);
            return;
        }
    }
}

void UecSrc::activate() {
    startFlow();
}

void UecSrc::setEndTrigger(Trigger& end_trigger) {
    _end_trigger = &end_trigger;
};

////////////////////////////////////////////////////////////////
//  UEC SINK PORT
////////////////////////////////////////////////////////////////
UecSinkPort::UecSinkPort(UecSink& sink, uint32_t port_num)
    : _sink(sink), _port_num(port_num) {
}

void UecSinkPort::setRoute(const Route& route) {
    _route = &route;
}

void UecSinkPort::receivePacket(Packet& pkt) {
    _sink.receivePacket(pkt, _port_num);
}

const string& UecSinkPort::nodename() {
    return _sink.nodename();
}

////////////////////////////////////////////////////////////////
//  UEC SINK
////////////////////////////////////////////////////////////////

UecSink::UecSink(TrafficLogger* trafficLogger, UecPullPacer* pullPacer, UecNIC& nic, uint32_t no_of_ports)
    : DataReceiver("uecSink"),
      _nic(nic),
      _flow(trafficLogger),
      _pullPacer(pullPacer),
      _expected_epsn(0),
      _high_epsn(0),
      _retx_backlog(0),
      _latest_pull(INIT_PULL),
      _highest_pull_target(INIT_PULL),
      _received_bytes(0),
      _accepted_bytes(0),
      _end_trigger(NULL),
      _epsn_rx_bitmap(0),
      _out_of_order_count(0),
      _recvd_bytes(0),
      _rcv_cwnd_pen(255),
      _ack_request(false) {
    
    _nodename = "uecSink";  // TBD: would be nice at add nodenum to nodename
    _no_of_ports = no_of_ports;
    _ports.resize(no_of_ports);
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        _ports[p] = new UecSinkPort(*this, p);
    }
        
    _stats = {0, 0, 0, 0, 0};
    _in_pull = false;
    _in_slow_pull = false;
}

UecSink::UecSink(TrafficLogger* trafficLogger,
                   linkspeed_bps linkSpeed,
                   double rate_modifier,
                   uint16_t mtu,
                   EventList& eventList,
                   UecNIC& nic,
                   uint32_t no_of_ports)
    : DataReceiver("uecSink"),
      _nic(nic),
      _flow(trafficLogger),
      _expected_epsn(0),
      _high_epsn(0),
      _retx_backlog(0),
      _latest_pull(INIT_PULL),
      _highest_pull_target(INIT_PULL),
      _received_bytes(0),
      _accepted_bytes(0),
      _end_trigger(NULL),
      _epsn_rx_bitmap(0),
      _out_of_order_count(0),
      _recvd_bytes(0),
      _rcv_cwnd_pen(255),
      _ack_request(false) {
    
    if (UecSrc::_receiver_based_cc)
        _pullPacer = new UecPullPacer(linkSpeed, rate_modifier, mtu, eventList, no_of_ports);
    else    
        _pullPacer = NULL;

    _no_of_ports = no_of_ports;
    _ports.resize(no_of_ports);
    for (uint32_t p = 0; p < _no_of_ports; p++) {
        _ports[p] = new UecSinkPort(*this, p);
    }
    _stats = {0, 0, 0, 0, 0};
    _in_pull = false;
    _in_slow_pull = false;
}

void UecSink::connectPort(uint32_t port_num, UecSrc& src, const Route& route) {
    _src = &src;
    _ports[port_num]->setRoute(route);
}

void UecSink::handlePullTarget(UecBasePacket::seq_t pt) {
    if (!UecSrc::_receiver_based_cc)
        return;

    if (_src->debug())
        cout << " UecSink " << _nodename << " src " << _src->nodename() << " handlePullTarget pt " << pt << " highest_pt " << _highest_pull_target << endl;
    if (pt > _highest_pull_target) {
        if (_src->debug())
            cout << "    pull target advanced\n";
        _highest_pull_target = pt;

        if (_retx_backlog == 0 && !_in_pull) {
            if (_src->debug())
                cout << "    requesting pull\n";
            _in_pull = true;
            _pullPacer->requestPull(this);
        }
    }
}

/*void UecSink::handleReceiveBitmap(){

}*/

void UecSink::processData(const UecDataPacket& pkt) {
    bool force_ack = false;

    if (_src->debug())
        cout << " UecSink " << _nodename << " src " << _src->nodename()
             << " processData: " << pkt.epsn() << " time " << timeAsNs(getSrc()->eventlist().now())
             << " when expected epsn is " << _expected_epsn << " ooo count " << _out_of_order_count
             << " flow " << _src->flow()->str() << endl;

    _accepted_bytes += pkt.size();

    handlePullTarget(pkt.pull_target());

    if (pkt.epsn() > _high_epsn) {
        // highest_received is used to bound the sack bitmap. This is a 64 bit number in simulation,
        // never wraps. In practice need to handle sequence number wrapping.
        _high_epsn = pkt.epsn();
    }

    // should send an ACK; if incoming packet is ECN marked, the ACK will be sent straight away;
    // otherwise ack will be delayed until we have cumulated enough bytes / packets.
    bool ecn = (bool)(pkt.flags() & ECN_CE);

    if (pkt.epsn() < _expected_epsn || _epsn_rx_bitmap[pkt.epsn()]) {
        if (UecSrc::_debug)
            cout << _nodename << " src " << _src->nodename() << " duplicate psn " << pkt.epsn()
                 << endl;

        _stats.duplicates++;
        _nic.logReceivedData(pkt.size(), 0);

        // sender is confused and sending us duplicates: ACK straight away.
        // this code is different from the proposed hardware implementation, as it keeps track of
        // the ACK state of OOO packets.
        UecAckPacket* ack_packet =
            sack(pkt.path_id(), ecn ? pkt.epsn() : sackBitmapBase(pkt.epsn()), pkt.epsn(), ecn);
        // ack_packet->sendOn();
        _nic.sendControlPacket(ack_packet, NULL, this);

        _accepted_bytes = 0;  // careful about this one.
        return;
    }

    if (_received_bytes == 0) {
        force_ack = true;
    }
    // packet is in window, count the bytes we got.
    // should only count for non RTS and non trimmed packets.
    _received_bytes += pkt.size() - UecAckPacket::ACKSIZE;
    _nic.logReceivedData(pkt.size(), pkt.size());

    _recvd_bytes += pkt.size();

    assert(_received_bytes <= _src->flowsize());
    if (_src->debug() && _received_bytes == _src->flowsize())
        cout << _nodename << " received " << _received_bytes << " at "
             << timeAsUs(EventList::getTheEventList().now()) << endl;

    if (pkt.ar()) {
        // this triggers an immediate ack; also triggers another ack later when the ooo queue drains
        // (_ack_request tracks this state)
        force_ack = true;
        _ack_request = true;
    }

    if (_src->debug())
        cout << _nodename << " src " << _src->nodename()
             << " >>    cumulative ack was: " << _expected_epsn << " flow " << _src->flow()->str()
             << endl;

    if (pkt.epsn() == _expected_epsn) {
        while (_epsn_rx_bitmap[++_expected_epsn]) {
            // clean OOO state, this will wrap at some point.
            _epsn_rx_bitmap[_expected_epsn] = 0;
            _out_of_order_count--;
        }
        if (_src->debug())
            cout << " UecSink " << _nodename << " src " << _src->nodename()
                 << " >>    cumulative ack now: " << _expected_epsn << " ooo count "
                 << _out_of_order_count << " flow " << _src->flow()->str() << endl;

        if (_out_of_order_count == 0 && _ack_request) {
            force_ack = true;
            _ack_request = false;
        }
    } else {
        _epsn_rx_bitmap[pkt.epsn()] = 1;
        _out_of_order_count++;
        _stats.out_of_order++;
    }

    if (ecn || shouldSack() || force_ack) {
        UecAckPacket* ack_packet =
            sack(pkt.path_id(), (ecn || pkt.ar()) ? pkt.epsn() : sackBitmapBase(pkt.epsn()), pkt.epsn(), ecn);

        if (_src->debug())
            cout << " UecSink " << _nodename << " src " << _src->nodename()
                 << " send ack now: " << _expected_epsn << " ooo count " << _out_of_order_count
                 << " flow " << _src->flow()->str() << endl;

        _accepted_bytes = 0;

        // ack_packet->sendOn();
        _nic.sendControlPacket(ack_packet, NULL, this);
    }
}

void UecSink::processTrimmed(const UecDataPacket& pkt) {
    _nic.logReceivedTrim(pkt.size());

    _stats.trimmed++;

    if (pkt.epsn() < _expected_epsn || _epsn_rx_bitmap[pkt.epsn()]) {
        if (_src->debug())
            cout << " UecSink processTrimmed got a packet we already have: " << pkt.epsn()
                 << " time " << timeAsNs(getSrc()->eventlist().now()) << " flow"
                 << _src->flow()->str() << endl;

        UecAckPacket* ack_packet = sack(pkt.path_id(), sackBitmapBase(pkt.epsn()), pkt.epsn(), false);
        //ack_packet->sendOn();
        _nic.sendControlPacket(ack_packet, NULL, this);
        return;
    }

    if (_src->debug())
        cout << " UecSink processTrimmed packet " << pkt.epsn() << " time "
             << timeAsNs(getSrc()->eventlist().now()) << " flow" << _src->flow()->str() << endl;

    handlePullTarget(pkt.pull_target());

    if (_src->debug())
        cout << "RTX_backlog++ trim: " << pkt.epsn() << " from " << getSrc()->nodename()
             << " rtx_backlog " << rtx_backlog() << " at " << timeAsUs(getSrc()->eventlist().now())
             << " flow " << _src->flow()->str() << endl;

    UecNackPacket* nack_packet = nack(pkt.path_id(), pkt.epsn());

    // nack_packet->sendOn();
    _nic.sendControlPacket(nack_packet, NULL, this);

    if (UecSrc::_receiver_based_cc && !_in_pull) {
        // source is now retransmitting, must add it to the list.
        if (_src->debug())
            cout << "PullPacer RequestPull: " << _src->flow()->str() << " at "
                 << timeAsUs(getSrc()->eventlist().now()) << endl;

        _in_pull = true;
        _pullPacer->requestPull(this);
    }
}

void UecSink::processRts(const UecRtsPacket& pkt) {
    assert(pkt.ar());
    if (_src->debug())
        cout << " UecSink " << _nodename << " src " << _src->nodename()
             << " processRts: " << pkt.epsn() << " time " << timeAsNs(getSrc()->eventlist().now()) << endl;

    handlePullTarget(pkt.pull_target());

    // what happens if this is not an actual retransmit, i.e. the host decides with the ACK that it
    // is

    if (_src->debug())
        cout << "RTX_backlog++ RTS: " << _src->flow()->str() << " rtx_backlog " << rtx_backlog()
             << " at " << timeAsUs(getSrc()->eventlist().now()) << endl;

    if (UecSrc::_receiver_based_cc && !_in_pull) {
        _in_pull = true;
        _pullPacer->requestPull(this);
    }

    bool ecn = (bool)(pkt.flags() & ECN_CE);
    assert(!ecn); // not expecting ECN set on control packets

    if (pkt.epsn() < _expected_epsn || _epsn_rx_bitmap[pkt.epsn()]) {
        if (_src->debug())
            cout << _nodename << " src " << _src->nodename() << " duplicate RTS psn " << pkt.epsn()
                 << endl;

        _stats.duplicates++;

        // sender is confused and sending us duplicates: ACK straight away.
        // this code is different from the proposed hardware implementation, as it keeps track of
        // the ACK state of OOO packets.
        UecAckPacket* ack_packet = sack(pkt.path_id(), sackBitmapBase(pkt.epsn()), pkt.epsn(), ecn);
        _nic.sendControlPacket(ack_packet, NULL, this);

        _accepted_bytes = 0;  // careful about this one.
        return;
    }


    if (pkt.epsn() == _expected_epsn) {
        while (_epsn_rx_bitmap[++_expected_epsn]) {
            // clean OOO state, this will wrap at some point.
            _epsn_rx_bitmap[_expected_epsn] = 0;
            _out_of_order_count--;
        }
        if (_src->debug())
            cout << " UecSink " << _nodename << " src " << _src->nodename()
                 << " >>    cumulative ack now: " << _expected_epsn << " ooo count "
                 << _out_of_order_count << " flow " << _src->flow()->str() << endl;

        if (_out_of_order_count == 0 && _ack_request) {
            _ack_request = false;
        }
    } else {
        _epsn_rx_bitmap[pkt.epsn()] = 1;
        _out_of_order_count++;
        _stats.out_of_order++;
    }

    UecAckPacket* ack_packet =
        sack(pkt.path_id(), (ecn || pkt.ar()) ? pkt.epsn() : sackBitmapBase(pkt.epsn()), pkt.epsn(), ecn);

    if (_src->debug())
        cout << " UecSink " << _nodename << " src " << _src->nodename()
             << " send ack now: " << _expected_epsn << " ooo count " << _out_of_order_count
             << " flow " << _src->flow()->str() << endl;

    _nic.sendControlPacket(ack_packet, NULL, this);
}

void UecSink::receivePacket(Packet& pkt, uint32_t port_num) {
    _stats.received++;
    _stats.bytes_received += pkt.size();  // should this include just the payload?

    switch (pkt.type()) {
        case UECDATA:
            if (pkt.header_only())
                processTrimmed((const UecDataPacket&)pkt);
            else
                processData((const UecDataPacket&)pkt);

            pkt.free();
            break;
        case UECRTS:
            processRts((const UecRtsPacket&)pkt);
            pkt.free();
            break;
        default:
            abort();
    }
}

uint16_t UecSink::nextEntropy() {
    int spraymask = (1 << TGT_EV_SIZE) - 1;
    int fixedmask = ~spraymask;
    int idx = _entropy & spraymask;
    int fixed_entropy = _entropy & fixedmask;
    int ev = idx++ & spraymask;

    _entropy = fixed_entropy | ev;  // save for next pkt

    return ev;
}

UecPullPacket* UecSink::pull() {
    // called when pull pacer is ready to give another credit to this connection.
    // TODO: need to credit in multiple of MTU here.

    if (_retx_backlog > 0) {
        if (_retx_backlog > UecSink::_credit_per_pull)
            _retx_backlog -= UecSink::_credit_per_pull;
        else
            _retx_backlog = 0;

        if (UecSrc::_debug)
            cout << "RTX_backlog--: " << getSrc()->nodename() << " rtx_backlog " << rtx_backlog()
                 << " at " << timeAsUs(getSrc()->eventlist().now()) << " flow "
                 << _src->flow()->str() << endl;
    }

    _latest_pull += UecSink::_credit_per_pull;

    UecPullPacket* pkt = NULL;
    pkt = UecPullPacket::newpkt(_flow, NULL, _latest_pull, nextEntropy(), _srcaddr);

    return pkt;
}

bool UecSink::shouldSack() {
    return _accepted_bytes >= _bytes_unacked_threshold;
}

UecBasePacket::seq_t UecSink::sackBitmapBase(UecBasePacket::seq_t epsn) {
    return max((int64_t)epsn - 63, (int64_t)(_expected_epsn + 1));
}

UecBasePacket::seq_t UecSink::sackBitmapBaseIdeal() {
    uint8_t lowest_value = UINT8_MAX;
    UecBasePacket::seq_t lowest_position;

    // find the lowest non-zero value in the sack bitmap; that is the candidate for the base, since
    // it is the oldest packet that we are yet to sack. on sack bitmap construction that covers a
    // given seqno, the value is incremented.
    for (UecBasePacket::seq_t crt = _expected_epsn; crt <= _high_epsn; crt++)
        if (_epsn_rx_bitmap[crt] && _epsn_rx_bitmap[crt] < lowest_value) {
            lowest_value = _epsn_rx_bitmap[crt];
            lowest_position = crt;
        }

    if (lowest_position + 64 > _high_epsn)
        lowest_position = _high_epsn - 64;

    if (lowest_position <= _expected_epsn)
        lowest_position = _expected_epsn + 1;

    return lowest_position;
}

uint64_t UecSink::buildSackBitmap(UecBasePacket::seq_t ref_epsn) {
    // take the next 64 entries from ref_epsn and create a SACK bitmap with them
    if (_src->debug())
        cout << " UecSink: building sack for ref_epsn " << ref_epsn << endl;
    uint64_t bitmap = (uint64_t)(_epsn_rx_bitmap[ref_epsn] != 0) << 63;

    for (int i = 1; i < 64; i++) {
        bitmap = bitmap >> 1 | (uint64_t)(_epsn_rx_bitmap[ref_epsn + i] != 0) << 63;
        if (_src->debug() && (_epsn_rx_bitmap[ref_epsn + i] != 0))
            cout << "     Sack: " << ref_epsn + i << endl;

        if (_epsn_rx_bitmap[ref_epsn + i]) {
            // remember that we sacked this packet
            if (_epsn_rx_bitmap[ref_epsn + i] < UINT8_MAX)
                _epsn_rx_bitmap[ref_epsn + i]++;
        }
    }
    if (_src->debug())
        cout << "       bitmap is: " << bitmap << endl;
    return bitmap;
}

UecAckPacket* UecSink::sack(uint16_t path_id, UecBasePacket::seq_t seqno, UecBasePacket::seq_t acked_psn, bool ce) {
    uint64_t bitmap = buildSackBitmap(seqno);
    UecAckPacket* pkt =
        UecAckPacket::newpkt(_flow, NULL, _expected_epsn, seqno, acked_psn, path_id, ce, _recvd_bytes,_rcv_cwnd_pen,_srcaddr);
    pkt->set_bitmap(bitmap);
    return pkt;
}

UecNackPacket* UecSink::nack(uint16_t path_id, UecBasePacket::seq_t seqno) {
    UecNackPacket* pkt = UecNackPacket::newpkt(_flow, NULL, seqno, path_id,  _recvd_bytes,_rcv_cwnd_pen,_srcaddr);
    return pkt;
}

void UecSink::setEndTrigger(Trigger& end_trigger) {
    _end_trigger = &end_trigger;
};

static unsigned pktByteTimes(unsigned size) {
    // IPG (96 bit times) + preamble + SFD + ether header + FCS = 38B
    return max(size, 46u) + 38;
}

uint32_t UecSink::reorder_buffer_size() {
    uint32_t count = 0;
    // it's not very efficient to count each time, but if we only do
    // this occasionally when the sink logger runs, it should be OK.
    for (uint32_t i = 0; i < uecMaxInFlightPkts; i++) {
        if (_epsn_rx_bitmap[i])
            count++;
    }
    return count;
}

////////////////////////////////////////////////////////////////
//  UEC PACER
////////////////////////////////////////////////////////////////

// pull rate modifier should generally be something like 0.99 so we pull at just less than line rate
UecPullPacer::UecPullPacer(linkspeed_bps linkSpeed,
                             double pull_rate_modifier,
                             uint16_t mtu,
                             EventList& eventList,
                             uint32_t no_of_ports)
    : EventSource(eventList, "uecPull"),
      _pktTime(pull_rate_modifier * 8 * pktByteTimes(mtu) * 1e12 / (linkSpeed * no_of_ports)) {
    _active = false;
}

void UecPullPacer::doNextEvent() {
    if (_active_senders.empty() && _idle_senders.empty()) {
        _active = false;
        return;
    }

    UecSink* sink = NULL;
    UecPullPacket* pullPkt;

    if (!_active_senders.empty()) {
        sink = _active_senders.front();

        assert(sink->inPullQueue());

        _active_senders.pop_front();
        pullPkt = sink->pull();

        // TODO if more pulls are needed, enqueue again
        if (UecSrc::_debug)
            cout << "PullPacer: Active: " << sink->getSrc()->nodename() << " backlog "
                 << sink->backlog() << " at " << timeAsUs(eventlist().now()) << endl;
        if (sink->backlog() > 0)
            _active_senders.push_back(sink);
        else {  // this sink has had its demand satisfied, move it to idle senders list.
            _idle_senders.push_back(sink);
            sink->removeFromPullQueue();
            sink->addToSlowPullQueue();
        }
    } else {  // no active senders, we must have at least one idle sender
        sink = _idle_senders.front();
        _idle_senders.pop_front();

        if (!sink->inSlowPullQueue())
            sink->addToSlowPullQueue();

        if (UecSrc::_debug)
            cout << "PullPacer: Idle: " << sink->getSrc()->nodename() << " at "
                 << timeAsUs(eventlist().now()) << " backlog " << sink->backlog() << " "
                 << sink->slowCredit() << " max "
                 << UecBasePacket::quantize_floor(sink->getMaxCwnd()) << endl;
        pullPkt = sink->pull();
        pullPkt->set_slow_pull(true);

        if (sink->backlog() == 0 &&
            sink->slowCredit() < UecBasePacket::quantize_floor(sink->getMaxCwnd())) {
            // only send upto 1BDP worth of speculative credit.
            // backlog will be negative once this source starts receiving speculative credit.
            _idle_senders.push_back(sink);
        } else
            sink->removeFromSlowPullQueue();
    }

    pullPkt->flow().logTraffic(*pullPkt, *this, TrafficLogger::PKT_SEND);

    // pullPkt->sendOn();
    sink->getNIC()->sendControlPacket(pullPkt, NULL, sink);
    _active = true;

    eventlist().sourceIsPendingRel(*this, _pktTime);
}

bool UecPullPacer::isActive(UecSink* sink) {
    for (auto i = _active_senders.begin(); i != _active_senders.end(); i++) {
        if (*i == sink)
            return true;
    }
    return false;
}

bool UecPullPacer::isIdle(UecSink* sink) {
    for (auto i = _idle_senders.begin(); i != _idle_senders.end(); i++) {
        if (*i == sink)
            return true;
    }
    return false;
}

void UecPullPacer::requestPull(UecSink* sink) {
    if (isActive(sink)) {
        abort();
    }
    assert(sink->inPullQueue());

    _active_senders.push_back(sink);
    // TODO ack timer

    if (!_active) {
        eventlist().sourceIsPendingRel(*this, 0);
        _active = true;
    }
}


