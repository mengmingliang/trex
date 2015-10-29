/*
 Itay Marom
 Cisco Systems, Inc.
*/

/*
Copyright (c) 2015-2015 Cisco Systems, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <trex_stateless.h>
#include <trex_stateless_port.h>
#include <string>

#ifndef TREX_RPC_MOCK_SERVER
// DPDK c++ issue 
#define UINT8_MAX 255
#define UINT16_MAX 0xFFFF
// DPDK c++ issue 
#endif

#include <rte_ethdev.h>
#include <os_time.h>

using namespace std;

/***************************
 * trex stateless port
 * 
 **************************/
TrexStatelessPort::TrexStatelessPort(uint8_t port_id) : m_port_id(port_id) {
    m_port_state = PORT_STATE_UP_IDLE;
    clear_owner();
}


/**
 * starts the traffic on the port
 * 
 */
TrexStatelessPort::rc_e
TrexStatelessPort::start_traffic(void) {

    if (m_port_state != PORT_STATE_UP_IDLE) {
        return (RC_ERR_BAD_STATE_FOR_OP);
    }

    if (get_stream_table()->size() == 0) {
        return (RC_ERR_NO_STREAMS);
    }

    m_port_state = PORT_STATE_TRANSMITTING;

    /* real code goes here */
    return (RC_OK);
}

void 
TrexStatelessPort::stop_traffic(void) {

    /* real code goes here */
    if (m_port_state == PORT_STATE_TRANSMITTING) {
        m_port_state = PORT_STATE_UP_IDLE;
    }
}

/**
* access the stream table
* 
*/
TrexStreamTable * TrexStatelessPort::get_stream_table() {
    return &m_stream_table;
}


std::string 
TrexStatelessPort::get_state_as_string() {

    switch (get_state()) {
    case PORT_STATE_DOWN:
        return "down";

    case PORT_STATE_UP_IDLE:
        return  "idle";

    case PORT_STATE_TRANSMITTING:
        return "transmitting";
    }

    return "unknown";
}

void
TrexStatelessPort::get_properties(string &driver, string &speed) {

    /* take this from DPDK */
    driver = "e1000";
    speed  = "1 Gbps";
}


/**
 * generate a random connection handler
 * 
 */
std::string 
TrexStatelessPort::generate_handler() {
    std::stringstream ss;

    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    /* generate 8 bytes of random handler */
    for (int i = 0; i < 8; ++i) {
        ss << alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return (ss.str());
}

/**
 * update stats for the port
 * 
 */
void 
TrexStatelessPort::update_stats() {
    struct rte_eth_stats stats;
    rte_eth_stats_get(m_port_id, &stats);

    /* copy straight values */
    m_stats.m_stats.m_total_tx_bytes = stats.obytes;
    m_stats.m_stats.m_total_rx_bytes = stats.ibytes;

    m_stats.m_stats.m_total_tx_pkts  = stats.opackets;
    m_stats.m_stats.m_total_rx_pkts  = stats.ipackets;

    /* calculate stats */
    m_stats.m_stats.m_tx_bps = m_stats.m_bw_tx_bps.add(stats.obytes);
    m_stats.m_stats.m_rx_bps = m_stats.m_bw_rx_bps.add(stats.ibytes);

    m_stats.m_stats.m_tx_pps = m_stats.m_bw_tx_pps.add(stats.opackets);
    m_stats.m_stats.m_rx_pps = m_stats.m_bw_rx_pps.add(stats.ipackets);

}

const TrexPortStats &
TrexStatelessPort::get_stats() {
    return m_stats;
}

void
TrexStatelessPort::encode_stats(Json::Value &port) {

    port["tx_bps"]          = m_stats.m_stats.m_tx_bps;
    port["rx_bps"]          = m_stats.m_stats.m_rx_bps;

    port["tx_pps"]          = m_stats.m_stats.m_tx_pps;
    port["rx_pps"]          = m_stats.m_stats.m_rx_pps;

    port["total_tx_pkts"]   = Json::Value::UInt64(m_stats.m_stats.m_total_tx_pkts);
    port["total_rx_pkts"]   = Json::Value::UInt64(m_stats.m_stats.m_total_rx_pkts);

    port["total_tx_bytes"]  = Json::Value::UInt64(m_stats.m_stats.m_total_tx_bytes);
    port["total_rx_bytes"]  = Json::Value::UInt64(m_stats.m_stats.m_total_rx_bytes);
    
    port["tx_rx_errors"]    = Json::Value::UInt64(m_stats.m_stats.m_tx_rx_errors);
}



/***************************
 * BW measurement
 * 
 **************************/
/* TODO: move this to a common place */
BWMeasure::BWMeasure() {
    reset();
}

void BWMeasure::reset(void) {
    m_start=false;
    m_last_time_msec=0;
    m_last_bytes=0;
    m_last_result=0.0;
};

double BWMeasure::calc_MBsec(uint32_t dtime_msec,
                             uint64_t dbytes){
    double rate=0.000008*( (  (double)dbytes*(double)os_get_time_freq())/((double)dtime_msec) );
    return(rate);
}

double BWMeasure::add(uint64_t size) {
    if ( false == m_start ) {
        m_start=true;
        m_last_time_msec = os_get_time_msec() ;
        m_last_bytes=size;
        return(0.0);
    }

    uint32_t ctime=os_get_time_msec();
    if ((ctime - m_last_time_msec) <os_get_time_freq() ) {
        return(m_last_result);
    }

    uint32_t dtime_msec = ctime-m_last_time_msec;
    uint64_t dbytes     = size - m_last_bytes;

    m_last_time_msec    = ctime;
    m_last_bytes        = size;

    m_last_result= 0.5*calc_MBsec(dtime_msec,dbytes) +0.5*(m_last_result);
    return( m_last_result );
}

