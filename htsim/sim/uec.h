// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef UEC_H
#define UEC_H

#include <memory>
#include <tuple>
#include <list>

#include "eventlist.h"
#include "trigger.h"
#include "uecpacket.h"
#include "circular_buffer.h"
#include "modular_vector.h"
#include "pciemodel.h"
#include "oversubscribed_cc.h"

#define timeInf 0
// min RTO bound in us
//  *** don't change this default - override it by calling UecSrc::setMinRTO()
#define DEFAULT_UEC_RTO_MIN 100

static const unsigned uecMaxInFlightPkts = 1 << 14;
class UecPullPacer;
class UecSink;
class UecSrc;
class UecLogger;

// UecNIC aggregates UecSrcs that are on the same NIC.  It round
// robins between active srcs when we're limited by the sending
// linkspeed due to outcast (or just at startup) - this avoids
// building an output queue like the old NDP simulator did, and so
// better models what happens in a h/w NIC.
class UecNIC : public EventSource, public NIC {
    struct PortData {
        simtime_picosec send_end_time;
        bool busy;
        mem_b last_pktsize;
    };
    struct CtrlPacket {
        UecBasePacket* pkt;
        UecSrc* src;
        UecSink* sink;
    };
public:
    UecNIC(id_t src_num, EventList& eventList, linkspeed_bps linkspeed, uint32_t ports);

    // handle traffic sources.
    const Route* requestSending(UecSrc& src);
    void startSending(UecSrc& src, mem_b pkt_size, const Route* rt);
    void cantSend(UecSrc& src);

    // handle control traffic from receivers.
    // only one of src or sink must be set
    void sendControlPacket(UecBasePacket* pkt, UecSrc* src, UecSink* sink);
    uint32_t findFreePort();
    void doNextEvent();

    linkspeed_bps linkspeed() const {return _linkspeed;}

    int activeSources() const { return _active_srcs.size(); }
    virtual const string& nodename() const {return _nodename;}
    list<UecSrc*> _active_srcs;

private:
    void sendControlPktNow();
    uint32_t sendOnFreePortNow(simtime_picosec endtime, const Route* rt);
    list<struct CtrlPacket> _control;
    mem_b _control_size;

    linkspeed_bps _linkspeed;
    int _num_queued_srcs;

    // data related to the NIC ports
    vector<struct PortData> _ports;
    uint32_t _rr_port;  // round robin last port we sent on
    uint32_t _no_of_ports;
    uint32_t _busy_ports;

    int _ratio_data, _ratio_control, _crt;

    string _nodename;
};

// Packets are received on ports, but then passed to the Src for handling
class UecSrcPort : public PacketSink {
public:
    UecSrcPort(UecSrc& src, uint32_t portnum);
    void setRoute(const Route& route);
    inline const Route* route() const {return _route;}
    virtual void receivePacket(Packet& pkt);
    virtual const string& nodename();
private:
    UecSrc& _src;
    uint8_t _port_num;
    const Route* _route;  // we're only going to support ECMP_HOST for now.
};

class UecSrc : public EventSource, public TriggerTarget {
public:
    struct Stats {
        /* all must be non-negative, but we'll make them signed so we
           can do maths with them without concern about underflow */
        int32_t new_pkts_sent;
        int32_t rtx_pkts_sent;
        int32_t rts_pkts_sent;
        int32_t rto_events;
        int32_t acks_received;
        int32_t nacks_received;
        int32_t pulls_received;
        int32_t bounces_received;
        int32_t rts_nacks;
    };
    UecSrc(TrafficLogger* trafficLogger, EventList& eventList, UecNIC& nic, uint32_t no_of_ports, bool rts = false);
    void delFromSendTimes(simtime_picosec time, UecDataPacket::seq_t seq_no);
    static void disableFairDecrease();
    /**
     * Initialize global NSCC parameters.
     */
    static void initNsccParams(simtime_picosec network_rtt, linkspeed_bps linkspeed, simtime_picosec target_Qdelay);
    /**
     * Initialize per-connection NSCC parameters.
     */
    void initNscc(mem_b cwnd, simtime_picosec peer_rtt=UecSrc::_network_rtt);
    /**
     * Initialize per-connection RCCC parameters.
     */
    void initRccc(mem_b cwnd,simtime_picosec peer_rtt=UecSrc::_network_rtt);

    void logFlowEvents(FlowEventLogger& flow_logger) { _flow_logger = &flow_logger; }
    virtual void connectPort(uint32_t portnum, Route& routeout, Route& routeback, UecSink& sink, simtime_picosec start);
    const Route* getPortRoute(uint32_t port_num) const {return _ports[port_num]->route();}
    UecSrcPort* getPort(uint32_t port_num) {return _ports[port_num];}
    void timeToSend(const Route& route);
    void receivePacket(Packet& pkt, uint32_t portnum);
    void doNextEvent();
    void setDst(uint32_t dst) { _dstaddr = dst; }
    static void setMinRTO(uint32_t min_rto_in_us) {
        _min_rto = timeFromUs((uint32_t)min_rto_in_us);
    }
    void setCwnd(mem_b cwnd) {
        //_maxwnd = cwnd;
        _cwnd = cwnd;
    }
    void setMaxWnd(mem_b maxwnd) {
        //_maxwnd = cwnd;
        _maxwnd = maxwnd;
    }

    void boundBaseRTT(simtime_picosec network_rtt){
        _base_rtt = network_rtt;
        _bdp = timeAsUs(_base_rtt) * _nic.linkspeed() / 8000000;
        _maxwnd =  1.5*_bdp;

        if (!_shown){
            cout << "Bound base RTT: _bdp " << _bdp << " _maxwnd " << _maxwnd << " _base_rtt " << timeAsUs(_base_rtt) << endl;
            _shown = true;
        }
    }
    mem_b maxWnd() const { return _maxwnd; }

    const Stats& stats() const { return _stats; }

    void setEndTrigger(Trigger& trigger);
    // called from a trigger to start the flow.
    virtual void activate();
    static uint32_t _path_entropy_size;  // now many paths do we include in our path set
    static int _global_node_count;
    static simtime_picosec _min_rto;
    static uint16_t _hdr_size;
    static uint16_t _mss;  // does not include header
    static uint16_t _mtu;  // does include header

    static bool _sender_based_cc;
    static bool _receiver_based_cc;

    enum Sender_CC { DCTCP, NSCC, CONSTANT};
    enum LoadBalancing_Algo { BITMAP, REPS, OBLIVIOUS, MIXED};
    enum PathFeedback {PATH_GOOD,PATH_ECN,PATH_NACK,PATH_TIMEOUT};
    enum EvState {STATE_GOOD,STATE_SKIP,STATE_ASSUMED_BAD};
    static Sender_CC _sender_cc_algo;
    static LoadBalancing_Algo _load_balancing_algo;

    static bool _enable_qa_gate;
    static bool _enable_avg_ecn_over_path;

    static bool _enable_fast_loss_recovery;

    virtual const string& nodename() { return _nodename; }
    inline void setFlowId(flowid_t flow_id) { _flow.set_flowid(flow_id); }
    void setFlowsize(uint64_t flow_size_in_bytes);
    mem_b flowsize() { return _flow_size; }
    inline PacketFlow* flow() { return &_flow; }

    inline flowid_t flowId() const { return _flow.flow_id(); }

    static bool _debug;
    static bool _shown;
    bool _debug_src;
    bool debug() const { return _debug_src; }

   private:
    UecNIC& _nic;
    uint32_t _no_of_ports;
    vector <UecSrcPort*> _ports;
    struct sendRecord {
        // need a constructor to be able to put this in a map
        sendRecord(mem_b psize, simtime_picosec stime) : pkt_size(psize), send_time(stime){};
        mem_b pkt_size;
        simtime_picosec send_time;
    };
    UecLogger* _logger;
    TrafficLogger* _pktlogger;
    FlowEventLogger* _flow_logger;
    Trigger* _end_trigger;

    // TODO in-flight packet storage - acks and sacks clear it
    // list<UecDataPacket*> _activePackets;

    // we need to access the in_flight packet list quickly by sequence number, or by send time.
    map<UecDataPacket::seq_t, sendRecord> _tx_bitmap;
    multimap<simtime_picosec, UecDataPacket::seq_t> _send_times;

    map<UecDataPacket::seq_t, mem_b> _rtx_queue;
    void startFlow();
    bool isSpeculative();
    void sendIfPermitted();
    mem_b sendPacket(const Route& route);
    mem_b sendNewPacket(const Route& route);
    mem_b sendRtxPacket(const Route& route);
    void sendRTS();
    void createSendRecord(UecDataPacket::seq_t seqno, mem_b pkt_size);
    void queueForRtx(UecBasePacket::seq_t seqno, mem_b pkt_size);
    void recalculateRTO();
    void startRTO(simtime_picosec send_time);
    void clearRTO();   // timer just expired, clear the state
    void cancelRTO();  // cancel running timer and clear state

    // not used, except for debugging timer issues
    void checkRTO() {
        if (_rtx_timeout_pending)
            assert(_rto_timer_handle != eventlist().nullHandle());
        else
            assert(_rto_timer_handle == eventlist().nullHandle());
    }

    void rtxTimerExpired();
    UecBasePacket::pull_quanta computePullTarget();
    void handlePull(UecBasePacket::pull_quanta pullno);
    mem_b handleAckno(UecDataPacket::seq_t ackno);
    mem_b handleCumulativeAck(UecDataPacket::seq_t cum_ack);
    void processAck(const UecAckPacket& pkt);
    void processNack(const UecNackPacket& pkt);
    void processPull(const UecPullPacket& pkt);
    void fastLossRecovery(uint32_t ooo, UecBasePacket::seq_t cum_ack);

    //added for NSCC
    void quick_adapt(bool trimmed);
    void updateCwndOnAck_NSCC(bool skip, simtime_picosec delay, mem_b newly_acked_bytes);
    void updateCwndOnNack_NSCC(bool skip, mem_b nacked_bytes);

    void updateCwndOnAck_DCTCP(bool skip, simtime_picosec delay, mem_b newly_acked_bytes);
    void updateCwndOnNack_DCTCP(bool skip, mem_b nacked_bytes);

    void dontUpdateCwndOnAck(bool skip, simtime_picosec delay, mem_b newly_acked_bytes);
    void dontUpdateCwndOnNack(bool skip, mem_b nacked_bytes);

    void (UecSrc::*updateCwndOnAck)(bool skip, simtime_picosec delay, mem_b newly_acked_bytes);
    void (UecSrc::*updateCwndOnNack)(bool skip, mem_b nacked_bytes);

    uint16_t nextEntropy_bitmap();
    uint16_t nextEntropy_REPS();
    uint16_t nextEntropy_oblivious();
    uint16_t nextEntropy_mixed();

    void processEv_bitmap(uint16_t path_id, PathFeedback feedback);
    void processEv_REPS(uint16_t path_id, PathFeedback feedback);
    void processEv_oblivious(uint16_t path_id, PathFeedback feedback);
    void processEv_mixed(uint16_t path_id, PathFeedback feedback);

    inline EvState ev_state(uint16_t path) const { 
        if (_ev_skip_bitmap[path]==0) 
            return STATE_GOOD; 
        else if (_ev_skip_bitmap[path]==_max_penalty) 
            return STATE_ASSUMED_BAD; 
        else 
            return STATE_SKIP;
    }

    uint16_t (UecSrc::*nextEntropy)();
    void (UecSrc::*processEv)(uint16_t path_id, PathFeedback feedback);

    bool checkFinished(UecDataPacket::seq_t cum_ack);

    Stats _stats;
    UecSink* _sink;

    // unlike in the NDP simulator, we maintain all the main quantities in bytes
    mem_b _flow_size;
    bool _done_sending;  // make sure we only trigger once
    mem_b _backlog;      // how much we need to send, not including retransmissions
    mem_b _rtx_backlog;
    mem_b _cwnd;
    mem_b _maxwnd;
    UecBasePacket::pull_quanta _pull_target;
    UecBasePacket::pull_quanta _pull;
    mem_b _credit;  // receive request credit in pull_quanta, but consume it in bytes
    inline mem_b credit() const;
    void stopSpeculating();
    void spendCredit(mem_b pktsize);
    UecDataPacket::seq_t _highest_sent;
    UecDataPacket::seq_t _highest_rtx_sent;
    mem_b _in_flight;
    mem_b _bdp;
    bool _send_blocked_on_nic;
    bool _speculating;

public:
    static linkspeed_bps _reference_network_linkspeed; 
    static simtime_picosec _reference_network_rtt; 
    static mem_b _reference_network_bdp; 
    static linkspeed_bps _network_linkspeed; 
    static simtime_picosec _network_rtt; 
    static mem_b _network_bdp; 
    // Smarttrack parameters
    static uint32_t _qa_scaling; 
    static simtime_picosec _target_Qdelay;
    static double _gamma;
    static uint32_t _pi;
    static double _alpha;
    // static double _scaling_c;
    // static double _fd;
    static double _fi;
    static double _fi_scale;
    static double _scaling_factor_a;
    static double _scaling_factor_b;
    static double _eta;
    static double _qa_threshold; 
    static double _ecn_alpha;
    static double _delay_alpha;
    // static double _ecn_thresh;
    static uint32_t _adjust_bytes_threshold;
    static simtime_picosec _adjust_period_threshold;
    //debug
    static flowid_t _debug_flowid;
private:
    bool quick_adapt(bool is_loss, simtime_picosec avgqdelay);
    void fair_increase(uint32_t newly_acked_bytes);
    void proportional_increase(uint32_t newly_acked_bytes,simtime_picosec delay);
    void fast_increase(uint32_t newly_acked_bytes,simtime_picosec delay);
    // void fair_decrease(bool can_decrease, uint32_t newly_acked_bytes);
    void multiplicative_decrease(uint32_t newly_acked_bytes);
    void fulfill_adjustment();
    void mark_packet_for_retransmission(UecBasePacket::seq_t psn, uint16_t pktsize);
    void update_delay(simtime_picosec delay, bool update_avg, bool skip);
    void update_base_rtt(simtime_picosec raw_rtt, uint16_t packet_size);
    simtime_picosec get_avg_delay();
    uint16_t get_avg_pktsize();
    void average_ecn_bytes(uint32_t pktsize, uint32_t newly_acked_bytes, bool skip);

    // entropy value calculation
    uint16_t _no_of_paths;       // must be a power of 2
    uint16_t _path_random;       // random upper bits of EV, set at startup and never changed
    uint16_t _path_xor;          // random value set each time we wrap the entropy values - XOR with
                                 // _current_ev_index
    uint16_t _current_ev_index;  // count through _no_of_paths and then wrap.  XOR with _path_xor to
                                 // get EV
    vector<uint8_t> _ev_skip_bitmap;  // paths scores for load balancing
    uint8_t _max_penalty;             // max value we allow in _path_penalties (typically 1 or 2).
    uint16_t _ev_skip_count;
    uint16_t _ev_bad_count;

    // RTT estimate data for RTO and sender based CC.
    simtime_picosec _rtt, _mdev, _rto, _raw_rtt;
    bool _rtx_timeout_pending;       // is the RTO running?
    simtime_picosec _rto_send_time;  // when we sent the oldest packet that the RTO is waiting on.
    simtime_picosec _rtx_timeout;    // when the RTO is currently set to expire
    simtime_picosec _last_rts;       // time when we last sent an RTS (or zero if never sent)
    EventList::Handle _rto_timer_handle;


    //used to drive ACK clock
    uint64_t _recvd_bytes;

    // Smarttrack sender based CC variables.
    simtime_picosec _base_rtt;
    mem_b _base_bdp;
    mem_b _achieved_bytes = 0;
    //used to trigger SmartTrack fulfill
    mem_b _received_bytes = 0;
    uint32_t _fi_count = 0;
    bool _trigger_qa = false;
    simtime_picosec _qa_endtime = 0;
    uint32_t _bytes_to_ignore = 0;
    uint32_t _bytes_ignored = 0;
    uint32_t _inc_bytes = 0;
    double _exp_avg_ecn = 0;
    simtime_picosec _avg_delay = 0;

    simtime_picosec _last_eta_time = 0;
    simtime_picosec _last_adjust_time = 0;
    bool _increase = false;
    simtime_picosec _last_dec_time = 0;
    uint32_t _highest_recv_seqno;
    bool _loss_recovery_mode = false;
    uint32_t _recovery_seqno = 0;
    uint32_t _loss_counter = 0;

    uint16_t _crt_path;
    list<uint16_t> _next_pathid;
    list<uint16_t> _knowngood_pathid;

    // Connectivity
    PacketFlow _flow;
    string _nodename;
    int _node_num;
    uint32_t _dstaddr;


};

// Packets are received on ports, but then passed to the Sink for handling
class UecSinkPort : public PacketSink {
public:
    UecSinkPort(UecSink& sink, uint32_t portnum);
    void setRoute(const Route& route);
    inline const Route* route() const {return _route;}
    virtual void receivePacket(Packet& pkt);
    virtual const string& nodename();
private:
    UecSink& _sink;
    uint8_t _port_num;
    const Route* _route;
};

class UecSink : public DataReceiver {
   public:
    struct Stats {
        uint64_t received;
        uint64_t bytes_received;
        uint64_t duplicates;
        uint64_t out_of_order;
        uint64_t trimmed;
        uint64_t pulls;
        uint64_t rts;
        uint64_t ecn_received;
        uint64_t ecn_bytes_received;
    };

    UecSink(TrafficLogger* trafficLogger, UecPullPacer* pullPacer, UecNIC& nic, uint32_t no_of_ports);
    UecSink(TrafficLogger* trafficLogger,
             linkspeed_bps linkSpeed,
             double rate_modifier,
             uint16_t mtu,
             EventList& eventList,
             UecNIC& nic, uint32_t no_of_ports);
    void receivePacket(Packet& pkt, uint32_t port_num);

    void processData(UecDataPacket& pkt);
    void processRts(const UecRtsPacket& pkt);
    void processTrimmed(const UecDataPacket& pkt);

    void handlePullTarget(UecBasePacket::seq_t pt);

    virtual const string& nodename() { return _nodename; }
    virtual uint64_t cumulative_ack() { return _expected_epsn; }
    virtual uint32_t drops() { return 0; }

    inline flowid_t flowId() const { return _flow.flow_id(); }

    UecPullPacket* pull();

    bool shouldSack();
    uint16_t unackedPackets();
    void setEndTrigger(Trigger& trigger);

    UecBasePacket::seq_t sackBitmapBase(UecBasePacket::seq_t epsn);
    UecBasePacket::seq_t sackBitmapBaseIdeal();
    uint64_t buildSackBitmap(UecBasePacket::seq_t ref_epsn);
    UecAckPacket* sack(uint16_t path_id, UecBasePacket::seq_t seqno, UecBasePacket::seq_t acked_psn, bool ce);

    UecNackPacket* nack(uint16_t path_id, UecBasePacket::seq_t seqno);

    UecBasePacket::pull_quanta backlog() {
        if (_highest_pull_target > _latest_pull)
            return _highest_pull_target - _latest_pull;
        else
            return 0;
    }
    UecBasePacket::pull_quanta slowCredit() {
        if (_highest_pull_target >= _latest_pull)
            return 0;
        else
            return _latest_pull - _highest_pull_target;
    }

    UecBasePacket::pull_quanta rtx_backlog() { return _retx_backlog; }
    const Stats& stats() const { return _stats; }
    void connectPort(uint32_t port_num, UecSrc& src, const Route& routeback);
    const Route* getPortRoute(uint32_t port_num) const {return _ports[port_num]->route();}
    UecSinkPort* getPort(uint32_t port_num) {return _ports[port_num];}
    void setSrc(uint32_t s) { _srcaddr = s; }
    inline void setFlowId(flowid_t flow_id) { _flow.set_flowid(flow_id); }

    inline bool inPullQueue() const { return _in_pull; }
    inline bool inSlowPullQueue() const { return _in_slow_pull; }

    inline void addToPullQueue() { _in_pull = true; }
    inline void removeFromPullQueue() { _in_pull = false; }
    inline void addToSlowPullQueue() {
        _in_pull = false;
        _in_slow_pull = true;
    }
    inline void removeFromSlowPullQueue() {
        _in_pull = false;
        _in_slow_pull = false;
    }
    inline UecNIC* getNIC() const { return &_nic; }

    inline void setPCIeModel(PCIeModel* c){assert(_model_pcie); _pcie = c;}
    inline void setOversubscribedCC(OversubscribedCC* c){_receiver_cc = c;}

    uint16_t nextEntropy();

    UecSrc* getSrc() { return _src; }
    uint32_t getMaxCwnd() { return _src->maxWnd(); };

    PCIeModel* pcieModel() const{ return _pcie;}

    static mem_b _bytes_unacked_threshold;
    static UecBasePacket::pull_quanta _credit_per_pull;
    static int TGT_EV_SIZE;

    static bool _receiver_oversubscribed_cc; 

    // for sink logger
    inline mem_b total_received() const { return _stats.bytes_received; }
    uint32_t reorder_buffer_size();  // count is in packets

    inline UecPullPacer* pullPacer() const {return _pullPacer;}

   private:
    uint32_t _no_of_ports;
    vector <UecSinkPort*> _ports;
    uint32_t _srcaddr;
    UecNIC& _nic;
    UecSrc* _src;
    PacketFlow _flow;
    UecPullPacer* _pullPacer;
    UecBasePacket::seq_t _expected_epsn;
    UecBasePacket::seq_t _high_epsn;
    UecBasePacket::seq_t
        _ref_epsn;  // used for SACK bitmap calculation in spec, unused here for NOW.
    UecBasePacket::pull_quanta _retx_backlog;
    UecBasePacket::pull_quanta _latest_pull;
    UecBasePacket::pull_quanta _highest_pull_target;

    bool _in_pull;       // this tunnel is in the pull queue.
    bool _in_slow_pull;  // this tunnel is in the slow pull queue.


    //received payload bytes, used to decide when flow has finished.
    mem_b _received_bytes;
    uint16_t _accepted_bytes;

    //used to help the sender slide his window.
    uint64_t _recvd_bytes;
    //used for flow control in sender CC mode. 
    //decides whether to reduce cwnd at sender; will change dynamically based on receiver resource availability. 
    uint8_t _rcv_cwnd_pen;

    Trigger* _end_trigger;
    ModularVector<uint8_t, uecMaxInFlightPkts>
        _epsn_rx_bitmap;  // list of packets above a hole, that we've received

    uint32_t _out_of_order_count;
    bool _ack_request;

    uint16_t _entropy;

    //variables for PCIe model
    PCIeModel* _pcie;
    OversubscribedCC* _receiver_cc;

    Stats _stats;
    string _nodename;

public:
    static bool _oversubscribed_cc;
    static bool _model_pcie;
};

class UecPullPacer : public EventSource {
   public:
    enum reason {PCIE = 0, OVERSUBSCRIBED_CC = 1};

    UecPullPacer(linkspeed_bps linkSpeed,
                  double pull_rate_modifier,
                  uint16_t mtu,
                  EventList& eventList,
                  uint32_t no_of_ports);
    void doNextEvent();
    void requestPull(UecSink* sink);

    bool isActive(UecSink* sink);
    bool isIdle(UecSink* sink);

    inline uint16_t mtu() const {return _mtu;}
    inline linkspeed_bps linkspeed() const {return _linkspeed;}

    void updatePullRate(reason r,double relative_rate);

    simtime_picosec packettime() const {return _actualPktTime;}

   private:
    list<UecSink*> _active_senders;  // TODO priorities?
    list<UecSink*> _idle_senders;    // TODO priorities?

    const simtime_picosec _pktTime;
    simtime_picosec _actualPktTime;
    bool _active;
    
    double _rates[2];

    linkspeed_bps _linkspeed;
    uint16_t _mtu;
};

#endif  // UEC_H
