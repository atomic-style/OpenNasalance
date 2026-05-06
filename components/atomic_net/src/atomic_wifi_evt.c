#include "atomic_wifi_evt.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include "esp_wifi_types_generic.h"


static const char *wifi_event_names[] = {[WIFI_EVENT_WIFI_READY] = "WIFI_READY",
                                         [WIFI_EVENT_SCAN_DONE] = "SCAN_DONE",
                                         [WIFI_EVENT_STA_START] = "STA_START",
                                         [WIFI_EVENT_STA_STOP] = "STA_STOP",
                                         [WIFI_EVENT_STA_CONNECTED] = "STA_CONNECTED",
                                         [WIFI_EVENT_STA_DISCONNECTED] = "STA_DISCONNECTED",
                                         [WIFI_EVENT_STA_AUTHMODE_CHANGE] = "STA_AUTHMODE_CHANGE",
                                         [WIFI_EVENT_STA_WPS_ER_SUCCESS] = "STA_WPS_ER_SUCCESS",
                                         [WIFI_EVENT_STA_WPS_ER_FAILED] = "STA_WPS_ER_FAILED",
                                         [WIFI_EVENT_STA_WPS_ER_TIMEOUT] = "STA_WPS_ER_TIMEOUT",
                                         [WIFI_EVENT_STA_WPS_ER_PIN] = "STA_WPS_ER_PIN",
                                         [WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP] = "STA_WPS_ER_PBC_OVERLAP",
                                         [WIFI_EVENT_AP_START] = "AP_START",
                                         [WIFI_EVENT_AP_STOP] = "AP_STOP",
                                         [WIFI_EVENT_AP_STACONNECTED] = "AP_STACONNECTED",
                                         [WIFI_EVENT_AP_STADISCONNECTED] = "AP_STADISCONNECTED",
                                         [WIFI_EVENT_AP_PROBEREQRECVED] = "AP_PROBEREQRECVED",
                                         [WIFI_EVENT_FTM_REPORT] = "FTM_REPORT",
                                         [WIFI_EVENT_STA_BSS_RSSI_LOW] = "STA_BSS_RSSI_LOW",
                                         [WIFI_EVENT_ACTION_TX_STATUS] = "ACTION_TX_STATUS",
                                         [WIFI_EVENT_ROC_DONE] = "ROC_DONE",
                                         [WIFI_EVENT_STA_BEACON_TIMEOUT] = "STA_BEACON_TIMEOUT",
                                         [WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START] = "CONNECTIONLESS_WAKE_START",
                                         [WIFI_EVENT_AP_WPS_RG_SUCCESS] = "AP_WPS_RG_SUCCESS",
                                         [WIFI_EVENT_AP_WPS_RG_FAILED] = "AP_WPS_RG_FAILED",
                                         [WIFI_EVENT_AP_WPS_RG_TIMEOUT] = "AP_WPS_RG_TIMEOUT",
                                         [WIFI_EVENT_AP_WPS_RG_PIN] = "AP_WPS_RG_PIN",
                                         [WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP] = "AP_WPS_RG_PBC_OVERLAP",
                                         [WIFI_EVENT_ITWT_SETUP] = "ITWT_SETUP",
                                         [WIFI_EVENT_ITWT_TEARDOWN] = "ITWT_TEARDOWN",
                                         [WIFI_EVENT_ITWT_PROBE] = "ITWT_PROBE",
                                         [WIFI_EVENT_ITWT_SUSPEND] = "ITWT_SUSPEND",
                                         [WIFI_EVENT_TWT_WAKEUP] = "TWT_WAKEUP",
                                         [WIFI_EVENT_BTWT_SETUP] = "BTWT_SETUP",
                                         [WIFI_EVENT_BTWT_TEARDOWN] = "BTWT_TEARDOWN",
                                         [WIFI_EVENT_NAN_SYNC_STARTED] = "NAN_SYNC_STARTED",
                                         [WIFI_EVENT_NAN_SYNC_STOPPED] = "NAN_SYNC_STOPPED",
                                         [WIFI_EVENT_NAN_SVC_MATCH] = "NAN_SVC_MATCH",
                                         [WIFI_EVENT_NAN_REPLIED] = "NAN_REPLIED",
                                         [WIFI_EVENT_NAN_RECEIVE] = "NAN_RECEIVE",
                                         [WIFI_EVENT_NDP_INDICATION] = "NDP_INDICATION",
                                         [WIFI_EVENT_NDP_CONFIRM] = "NDP_CONFIRM",
                                         [WIFI_EVENT_NDP_TERMINATED] = "NDP_TERMINATED",
                                         [WIFI_EVENT_HOME_CHANNEL_CHANGE] = "HOME_CHANNEL_CHANGE",
                                         [WIFI_EVENT_STA_NEIGHBOR_REP] = "STA_NEIGHBOR_REP",
                                         [WIFI_EVENT_AP_WRONG_PASSWORD] = "AP_WRONG_PASSWORD",
                                         [WIFI_EVENT_DPP_URI_READY] = "DPP_URI_READY",
                                         [WIFI_EVENT_DPP_CFG_RECVD] = "DPP_CFG_RECVD",
                                         [WIFI_EVENT_DPP_FAILED] = "DPP_FAILED",
                                         [WIFI_EVENT_MAX] = "INVALID_EVENT"};

static const char *wifi_err_reason[] = {
    [WIFI_REASON_UNSPECIFIED] = "WIFI_REASON_UNSPECIFIED",
    [WIFI_REASON_AUTH_EXPIRE] = "WIFI_REASON_AUTH_EXPIRE",                                     /**< Authentication expired */
    [WIFI_REASON_AUTH_LEAVE] = "WIFI_REASON_AUTH_LEAVE",                                       /**< Deauthentication due to leaving */
    [WIFI_REASON_ASSOC_TOOMANY] = "WIFI_REASON_ASSOC_TOOMANY",                                 /**< Too many associated stations */
    [WIFI_REASON_ASSOC_LEAVE] = "WIFI_REASON_ASSOC_LEAVE",                                     /**< Deassociated due to leaving */
    [WIFI_REASON_ASSOC_NOT_AUTHED] = "WIFI_REASON_ASSOC_NOT_AUTHED",                           /**< Association but not authenticated */
    [WIFI_REASON_DISASSOC_PWRCAP_BAD] = "WIFI_REASON_DISASSOC_PWRCAP_BAD",                     /**< Disassociated due to poor power capability */
    [WIFI_REASON_DISASSOC_SUPCHAN_BAD] = "WIFI_REASON_DISASSOC_SUPCHAN_BAD",                   /**< Disassociated due to unsupported channel */
    [WIFI_REASON_BSS_TRANSITION_DISASSOC] = "WIFI_REASON_BSS_TRANSITION_DISASSOC",             /**< Disassociated due to BSS transition */
    [WIFI_REASON_IE_INVALID] = "WIFI_REASON_IE_INVALID",                                       /**< Invalid Information Element (IE) */
    [WIFI_REASON_MIC_FAILURE] = "WIFI_REASON_MIC_FAILURE",                                     /**< MIC failure */
    [WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT] = "WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT",               /**< 4-way handshake timeout */
    [WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT] = "WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT",           /**< Group key update timeout */
    [WIFI_REASON_IE_IN_4WAY_DIFFERS] = "WIFI_REASON_IE_IN_4WAY_DIFFERS",                       /**< IE differs in 4-way handshake */
    [WIFI_REASON_GROUP_CIPHER_INVALID] = "WIFI_REASON_GROUP_CIPHER_INVALID",                   /**< Invalid group cipher */
    [WIFI_REASON_PAIRWISE_CIPHER_INVALID] = "WIFI_REASON_PAIRWISE_CIPHER_INVALID",             /**< Invalid pairwise cipher */
    [WIFI_REASON_AKMP_INVALID] = "WIFI_REASON_AKMP_INVALID",                                   /**< Invalid AKMP */
    [WIFI_REASON_UNSUPP_RSN_IE_VERSION] = "WIFI_REASON_UNSUPP_RSN_IE_VERSION",                 /**< Unsupported RSN IE version */
    [WIFI_REASON_INVALID_RSN_IE_CAP] = "WIFI_REASON_INVALID_RSN_IE_CAP",                       /**< Invalid RSN IE capabilities */
    [WIFI_REASON_802_1X_AUTH_FAILED] = "WIFI_REASON_802_1X_AUTH_FAILED",                       /**< 802.1X authentication failed */
    [WIFI_REASON_CIPHER_SUITE_REJECTED] = "WIFI_REASON_CIPHER_SUITE_REJECTED",                 /**< Cipher suite rejected */
    [WIFI_REASON_TDLS_PEER_UNREACHABLE] = "WIFI_REASON_TDLS_PEER_UNREACHABLE",                 /**< TDLS peer unreachable */
    [WIFI_REASON_TDLS_UNSPECIFIED] = "WIFI_REASON_TDLS_UNSPECIFIED",                           /**< TDLS unspecified */
    [WIFI_REASON_SSP_REQUESTED_DISASSOC] = "WIFI_REASON_SSP_REQUESTED_DISASSOC",               /**< SSP requested disassociation */
    [WIFI_REASON_NO_SSP_ROAMING_AGREEMENT] = "WIFI_REASON_NO_SSP_ROAMING_AGREEMENT",           /**< No SSP roaming agreement */
    [WIFI_REASON_BAD_CIPHER_OR_AKM] = "WIFI_REASON_BAD_CIPHER_OR_AKM",                         /**< Bad cipher or AKM */
    [WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION] = "WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION",   /**< Not authorized in this location */
    [WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS] = "WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS",     /**< Service change precludes TS */
    [WIFI_REASON_UNSPECIFIED_QOS] = "WIFI_REASON_UNSPECIFIED_QOS",                             /**< Unspecified QoS reason */
    [WIFI_REASON_NOT_ENOUGH_BANDWIDTH] = "WIFI_REASON_NOT_ENOUGH_BANDWIDTH",                   /**< Not enough bandwidth */
    [WIFI_REASON_MISSING_ACKS] = "WIFI_REASON_MISSING_ACKS",                                   /**< Missing ACKs */
    [WIFI_REASON_EXCEEDED_TXOP] = "WIFI_REASON_EXCEEDED_TXOP",                                 /**< Exceeded TXOP */
    [WIFI_REASON_STA_LEAVING] = "WIFI_REASON_STA_LEAVING",                                     /**< Station leaving */
    [WIFI_REASON_END_BA] = "WIFI_REASON_END_BA",                                               /**< End of Block Ack (BA) */
    [WIFI_REASON_UNKNOWN_BA] = "WIFI_REASON_UNKNOWN_BA",                                       /**< Unknown Block Ack (BA) */
    [WIFI_REASON_TIMEOUT] = "WIFI_REASON_TIMEOUT",                                             /**< Timeout */
    [WIFI_REASON_PEER_INITIATED] = "WIFI_REASON_PEER_INITIATED",                               /**< Peer initiated disassociation */
    [WIFI_REASON_AP_INITIATED] = "WIFI_REASON_AP_INITIATED",                                   /**< AP initiated disassociation */
    [WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT] = "WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT", /**< Invalid FT action frame count */
    [WIFI_REASON_INVALID_PMKID] = "WIFI_REASON_INVALID_PMKID",                                 /**< Invalid PMKID */
    [WIFI_REASON_INVALID_MDE] = "WIFI_REASON_INVALID_MDE",                                     /**< Invalid MDE */
    [WIFI_REASON_INVALID_FTE] = "WIFI_REASON_INVALID_FTE",                                     /**< Invalid FTE */
    [WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED] = "WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED", /**< Transmission link establishment failed */
    [WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED] = "WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED",               /**< Alternative channel occupied */
    [WIFI_REASON_BEACON_TIMEOUT] = "WIFI_REASON_BEACON_TIMEOUT",                                         /**< Beacon timeout */
    [WIFI_REASON_NO_AP_FOUND] = "WIFI_REASON_NO_AP_FOUND",                                               /**< No AP found */
    [WIFI_REASON_AUTH_FAIL] = "WIFI_REASON_AUTH_FAIL",                                                   /**< Authentication failed */
    [WIFI_REASON_ASSOC_FAIL] = "WIFI_REASON_ASSOC_FAIL",                                                 /**< Association failed */
    [WIFI_REASON_HANDSHAKE_TIMEOUT] = "WIFI_REASON_HANDSHAKE_TIMEOUT",                                   /**< Handshake timeout */
    [WIFI_REASON_CONNECTION_FAIL] = "WIFI_REASON_CONNECTION_FAIL",                                       /**< Connection failed */
    [WIFI_REASON_AP_TSF_RESET] = "WIFI_REASON_AP_TSF_RESET",                                             /**< AP TSF reset */
    [WIFI_REASON_ROAMING] = "WIFI_REASON_ROAMING",                                                       /**< Roaming */
    [WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG] = "WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG",             /**< Association comeback time too long */
    [WIFI_REASON_SA_QUERY_TIMEOUT] = "WIFI_REASON_SA_QUERY_TIMEOUT",                                     /**< SA query timeout */
    [WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY] = "WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY",   /**< No AP found with compatible security */
    [WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD] = "WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD",   /**< No AP found in auth mode threshold */
    [WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD] = "WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD",           /**< No AP found in RSSI threshold */

};

static const char *ip_event_names[] = {
    [IP_EVENT_STA_GOT_IP] = "IP_EVENT_STA_GOT_IP",
    [IP_EVENT_STA_LOST_IP] = "IP_EVENT_STA_LOST_IP",
    [IP_EVENT_AP_STAIPASSIGNED] = "IP_EVENT_AP_STAIPASSIGNED", /*!< soft-AP assign an IP to a connected station */
    [IP_EVENT_GOT_IP6] = "IP_EVENT_GOT_IP6",                   /*!< station or ap or ethernet interface v6IP addr is preferred */
    [IP_EVENT_ETH_GOT_IP] = "IP_EVENT_ETH_GOT_IP",             /*!< ethernet got IP from connected AP */
    [IP_EVENT_ETH_LOST_IP] = "IP_EVENT_ETH_LOST_IP",           /*!< ethernet lost IP and the IP is reset to 0 */
    [IP_EVENT_PPP_GOT_IP] = "IP_EVENT_PPP_GOT_IP",             /*!< PPP interface got IP */
    [IP_EVENT_PPP_LOST_IP] = "IP_EVENT_PPP_LOST_IP",           /*!< PPP interface lost IP */
    [IP_EVENT_TX_RX] = "IP_EVENT_TX_RX",                       /*!< transmitting/receiving data packet */
};

const char *a_wifi_evt_name(wifi_event_t evt_id) {
    if (evt_id < WIFI_EVENT_MAX) {
        const char *n = wifi_event_names[evt_id];
        return n ? n : "UNMAPPED_WIFI_EVENT";
    }
    return "UNKNOWN_WIFI_EVENT";
}

const char *a_wifi_err_reason(wifi_err_reason_t reason) {
    if (reason < sizeof(wifi_err_reason) / sizeof(wifi_err_reason[0])) {
        const char *n = wifi_err_reason[reason];
        return n ? n : "UNMAPPED_WIFI_ERR_REASON";
    }
    return "UNKNOWN_WIFI_ERR_REASON";
}

const char *a_ip_evt_name(ip_event_t evt_id) {
    if (evt_id < sizeof(ip_event_names) / sizeof(ip_event_names[0])) {
        const char *n = ip_event_names[evt_id];
        return n ? n : "UNMAPPED_IP_EVENT";
    }
    return "UNKNOWN_IP_EVENT";
}