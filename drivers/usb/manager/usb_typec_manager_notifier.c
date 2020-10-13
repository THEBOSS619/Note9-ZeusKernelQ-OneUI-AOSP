#include <linux/device.h>
#include <linux/module.h>

#include <linux/notifier.h>
#include <linux/usb/manager/usb_typec_manager_notifier.h>

#include <linux/ccic/ccic_core.h>
#include <linux/ccic/ccic_notifier.h>

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/sec_sysfs.h>
#include <linux/sec_batt.h>

#if defined(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif
#include <linux/usb_notify.h>
#include <linux/delay.h>
#if defined(CONFIG_CCIC_S2MM005)
#include <linux/ccic/s2mm005_ext.h>
#endif
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#if defined(CONFIG_CCIC_MAX77705)
#include <linux/ccic/max77705_usbc.h>
#include <linux/ccic/max77705_alternate.h>
#endif
#include <linux/of.h>

/* dwc3 irq storm patch */
/* need to check dwc3 link state during dcd time out case */
extern int dwc3_gadget_get_cmply_link_state_wrapper(void);
#define DEBUG
#define SET_MANAGER_NOTIFIER_BLOCK(nb, fn, dev) do {	\
		(nb)->notifier_call = (fn);		\
		(nb)->priority = (dev);			\
	} while (0)

#define DESTROY_MANAGER_NOTIFIER_BLOCK(nb)			\
		SET_MANAGER_NOTIFIER_BLOCK(nb, NULL, -1)

typedef enum
{
	VBUS_NOTIFIER,
	CCIC_NOTIFIER,
	MUIC_NOTIFIER
}notifier_register;

static int manager_notifier_init_done = 0;
static int confirm_manager_notifier_register = 0;
static int manager_notifier_init(void);

struct device *manager_device;
extern struct device *ccic_device;
manager_data_t typec_manager;
void set_usb_enumeration_state(int state);
static void cable_type_check_work(bool state, int time);
void calc_duration_time(unsigned long sTime, unsigned long eTime, unsigned long *dTime);
void wVbus_time_update(int mode);
void water_dry_time_update(int mode);

static int manager_notifier_notify(void *data)
{
	MANAGER_NOTI_TYPEDEF manager_noti = *(MANAGER_NOTI_TYPEDEF *)data;
	int ret = 0;

	pr_info("usb: [M] %s: src:%s dest:%s id:%s "
		"sub1:%02x sub2:%02x sub3:%02x\n", __func__,
		(manager_noti.src<CCIC_NOTI_DEST_NUM)?
		CCIC_NOTI_DEST_Print[manager_noti.src]:"unknown",
		(manager_noti.dest<CCIC_NOTI_DEST_NUM)?
		CCIC_NOTI_DEST_Print[manager_noti.dest]:"unknown",
		(manager_noti.id<CCIC_NOTI_ID_NUM)?
		CCIC_NOTI_ID_Print[manager_noti.id]:"unknown",
		manager_noti.sub1, manager_noti.sub2, manager_noti.sub3);

	if (manager_noti.dest == CCIC_NOTIFY_DEV_DP) {
		if (manager_noti.id == CCIC_NOTIFY_ID_DP_CONNECT) {
			typec_manager.dp_attach_state = manager_noti.sub1;
			typec_manager.dp_vid = manager_noti.sub2;
			typec_manager.dp_pid = manager_noti.sub3;
			typec_manager.dp_is_connect = 0;
			typec_manager.dp_hs_connect = 0;
		} else if (manager_noti.id == CCIC_NOTIFY_ID_DP_HPD) {
			typec_manager.dp_hpd_state = manager_noti.sub1;
		} else if (manager_noti.id == CCIC_NOTIFY_ID_DP_LINK_CONF) {
			typec_manager.dp_cable_type = manager_noti.sub1;
		}
	}

	if (manager_noti.dest == CCIC_NOTIFY_DEV_USB_DP) {
		if (manager_noti.id == CCIC_NOTIFY_ID_USB_DP) {
			typec_manager.dp_is_connect = manager_noti.sub1;
			typec_manager.dp_hs_connect = manager_noti.sub2;
		}
	}

	if (manager_noti.dest == CCIC_NOTIFY_DEV_USB) {
		if (typec_manager.ccic_drp_state == manager_noti.sub2)
			return 0;
		typec_manager.ccic_drp_state = manager_noti.sub2;
		if (typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_DETACH)
			set_usb_enumeration_state(0);
	}

	if (manager_noti.dest == CCIC_NOTIFY_DEV_BATTERY
		&& manager_noti.sub3 == typec_manager.water_cable_type) {
		if (manager_noti.sub1 != typec_manager.wVbus_det) {
			typec_manager.wVbus_det = manager_noti.sub1;
			typec_manager.waterChg_count += manager_noti.sub1;
			wVbus_time_update(typec_manager.wVbus_det);
		} else {
			return 0;
		}
	}

#ifdef CONFIG_USB_NOTIFY_PROC_LOG
	store_usblog_notify(NOTIFY_MANAGER, (void*)data , NULL);
#endif

	if (manager_noti.dest == CCIC_NOTIFY_DEV_MUIC) {
		ret = blocking_notifier_call_chain(&(typec_manager.manager_muic_notifier),
					manager_noti.id, &manager_noti);
	} else {
		ret = blocking_notifier_call_chain(&(typec_manager.manager_ccic_notifier),
					manager_noti.id, &manager_noti);
	}

	switch (ret) {
	case NOTIFY_DONE:
	case NOTIFY_OK:
		pr_info("usb: [M] %s: notify done(0x%x)\n", __func__, ret);
		break;
	case NOTIFY_STOP_MASK:
	case NOTIFY_BAD:
	default:
		if ( manager_noti.dest == CCIC_NOTIFY_DEV_USB) {
			pr_info("usb: [M] %s: UPSM case (0x%x)\n", __func__, ret);
			typec_manager.is_UFPS = 1;
		} else {
			pr_info("usb: [M] %s: notify error occur(0x%x)\n", __func__, ret);
		}
		break;
	}

	return ret;
}

void set_usb_enumeration_state(int state)
{
	if(typec_manager.usb_enum_state != state) {
		typec_manager.usb_enum_state = state;

		if(typec_manager.usb_enum_state == 0x310)
			typec_manager.usb310_count++;
		else if(typec_manager.usb_enum_state == 0x210)
			typec_manager.usb210_count++;
	}
}
EXPORT_SYMBOL(set_usb_enumeration_state);

bool get_usb_enumeration_state(void)
{
	return typec_manager.usb_enum_state? 1: 0;
}
EXPORT_SYMBOL(get_usb_enumeration_state);

int get_ccic_water_count(void)
{
	int ret;
	ret = typec_manager.water_count;
	typec_manager.water_count = 0;
	return ret;
}
EXPORT_SYMBOL(get_ccic_water_count);

int get_ccic_dry_count(void)
{
	int ret;
	ret = typec_manager.dry_count;
	typec_manager.dry_count = 0;
	return ret;
}
EXPORT_SYMBOL(get_ccic_dry_count);

int get_usb210_count(void)
{
	int ret;
	ret = typec_manager.usb210_count;
	typec_manager.usb210_count = 0;
	return ret;
}
EXPORT_SYMBOL(get_usb210_count);

int get_usb310_count(void)
{
	int ret;
	ret = typec_manager.usb310_count;
	typec_manager.usb310_count = 0;
	return ret;
}
EXPORT_SYMBOL(get_usb310_count);

int get_waterchg_count(int is_lpm)
{
    unsigned int ret = 0, islpcharge = 0;

#if defined(CONFIG_BATTERY_SAMSUNG)
    islpcharge = lpcharge?1:0;
#endif
    if (is_lpm != islpcharge)
        return 0;
    
    ret = typec_manager.waterChg_count;
    typec_manager.waterChg_count = 0;
    return ret;
}
EXPORT_SYMBOL(get_waterchg_count);

unsigned long get_waterdet_duration(void)
{
	unsigned long ret;
	struct timeval time;

	if (typec_manager.water_det) {
		do_gettimeofday(&time);
		calc_duration_time(typec_manager.waterDet_time,
			time.tv_sec, &typec_manager.waterDet_duration);
		typec_manager.waterDet_time = time.tv_sec;
	}

	ret = typec_manager.waterDet_duration/60;  /* min */
	typec_manager.waterDet_duration -= ret*60;
	return ret;
}
EXPORT_SYMBOL(get_waterdet_duration);

unsigned long get_wvbus_duration(int is_lpm)
{
	unsigned long ret = 0, islpcharge = 0;
	struct timeval time;

#if defined(CONFIG_BATTERY_SAMSUNG)
	islpcharge = lpcharge?1:0;
#endif
	if (is_lpm != islpcharge)
	    return 0;

	if (typec_manager.wVbus_det) {
		do_gettimeofday(&time);	/* time.tv_sec */
		calc_duration_time(typec_manager.wVbusHigh_time,
			time.tv_sec, &typec_manager.wVbus_duration);
		typec_manager.wVbusHigh_time = time.tv_sec;
	}

	ret = typec_manager.wVbus_duration;  /* sec */
	typec_manager.wVbus_duration = 0;
	return ret;
}
EXPORT_SYMBOL(get_wvbus_duration);

void set_usb_enable_state(void)
{
	if (!typec_manager.usb_enable_state) {
		typec_manager.usb_enable_state = true;
		if (typec_manager.pd_con_state)
			cable_type_check_work(true, 120);
		else if (typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_UFP && 
			typec_manager.cable_type == MANAGER_NOTIFY_MUIC_TIMEOUT_OPEN_DEVICE)
			cable_type_check_work(true, 10);
	}
}
EXPORT_SYMBOL(set_usb_enable_state);

void calc_duration_time(unsigned long sTime, unsigned long eTime, unsigned long *dTime)
{
	unsigned long calcDtime;

	calcDtime = eTime - sTime;

	/* check for exception case. */
	if (calcDtime > 86400)
		calcDtime = 0;

	*dTime += calcDtime;
}

void manager_notifier_usbdp_support(void)
{

	MANAGER_NOTI_TYPEDEF m_noti;
	if( typec_manager.dp_check_done == 1 ) {
		m_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		m_noti.dest = CCIC_NOTIFY_DEV_USB_DP;
		m_noti.id = CCIC_NOTIFY_ID_USB_DP;
		m_noti.sub1 = typec_manager.dp_is_connect;
		m_noti.sub2 = typec_manager.dp_hs_connect;
		m_noti.sub3 = 0;
		m_noti.pd = NULL;
		manager_notifier_notify(&m_noti);
		typec_manager.dp_check_done = 0;
	}
	return;
}

static int manager_external_notifier_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
	CC_NOTI_ATTACH_TYPEDEF p_batt_noti;
	int ret = 0;
	int enable = *(int *)data;

	switch (action) {
	case EXTERNAL_NOTIFY_DEVICEADD:
		pr_info("%s EXTERNAL_NOTIFY_DEVICEADD, enable=%d\n", __func__, enable);
		if (enable &&
			typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_DFP &&
			typec_manager.ccic_attach_state == CCIC_NOTIFY_ATTACH &&
			typec_manager.muic_action != MUIC_NOTIFY_CMD_DETACH) {
			pr_info("%s: a usb device is added in host mode\n", __func__);
			/* USB cable Type */
			p_batt_noti.src = CCIC_NOTIFY_DEV_MANAGER;
			p_batt_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
			p_batt_noti.id = CCIC_NOTIFY_ID_USB;
			p_batt_noti.attach = 0;
			p_batt_noti.rprd = 0;
			p_batt_noti.cable_type = PD_USB_TYPE;
			p_batt_noti.pd = NULL;
			manager_notifier_notify(&p_batt_noti);
		}
		break;
	default:
		break;
	}

	return ret;
}

static void cable_type_check(struct work_struct *work)
{
	CC_NOTI_USB_STATUS_TYPEDEF p_usb_noti;
	CC_NOTI_ATTACH_TYPEDEF p_batt_noti;
	int dwc3_link_check = 0;

	dwc3_link_check= dwc3_gadget_get_cmply_link_state_wrapper();
	if ( (typec_manager.ccic_drp_state != USB_STATUS_NOTIFY_ATTACH_UFP) ||
		typec_manager.is_UFPS || dwc3_link_check == 1 ){
		pr_info("usb: [M] %s: skip case : dwc3_link = %d\n", __func__, dwc3_link_check);
		return;
	}
	pr_info("usb: [M] %s: usb=%d, pd=%d cable_type=%d, dwc3_link_check=%d\n", __func__, typec_manager.usb_enum_state, typec_manager.pd_con_state, typec_manager.cable_type, dwc3_link_check);

	if(!typec_manager.usb_enum_state ||
		(typec_manager.muic_data_refresh
		&& typec_manager.cable_type==MANAGER_NOTIFY_MUIC_CHARGER)) {

		/* TA cable Type */
		p_usb_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		p_usb_noti.dest = CCIC_NOTIFY_DEV_USB;
		p_usb_noti.id = CCIC_NOTIFY_ID_USB;
		p_usb_noti.attach = CCIC_NOTIFY_DETACH;
		p_usb_noti.drp = USB_STATUS_NOTIFY_DETACH;
		p_usb_noti.sub3 = 0;
		p_usb_noti.pd = NULL;
		manager_notifier_notify(&p_usb_noti);

	} else {
		/* USB cable Type */
		p_batt_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		p_batt_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
		p_batt_noti.id = CCIC_NOTIFY_ID_USB;
		p_batt_noti.attach = 0;
		p_batt_noti.rprd = 0;
		p_batt_noti.cable_type = PD_USB_TYPE;
		p_batt_noti.pd = NULL;
		manager_notifier_notify(&p_batt_noti);
	}
}

static void cable_type_check_work(bool state, int time) {
	if(typec_manager.usb_enable_state) {
		cancel_delayed_work_sync(&typec_manager.cable_check_work);
		if(state) {
			queue_delayed_work(system_power_efficient_wq, &typec_manager.cable_check_work, msecs_to_jiffies(time*100));
		}
	}
}

static void muic_work_without_ccic(struct work_struct *work)
{
	CC_NOTI_USB_STATUS_TYPEDEF p_usb_noti;

	pr_info("usb: [M] %s: working state=%d, vbus=%s\n", __func__,
		typec_manager.muic_attach_state_without_ccic,
		typec_manager.vbus_state == STATUS_VBUS_HIGH ? "HIGH" : "LOW");

	if (typec_manager.muic_attach_state_without_ccic) {
		switch (typec_manager.muic_action) {
			case MUIC_NOTIFY_CMD_ATTACH:
#if defined(CONFIG_VBUS_NOTIFIER)
				if(typec_manager.vbus_state == STATUS_VBUS_HIGH)
#endif
				{
					p_usb_noti.src = CCIC_NOTIFY_DEV_MUIC;
					p_usb_noti.dest = CCIC_NOTIFY_DEV_USB;
					p_usb_noti.id = CCIC_NOTIFY_ID_USB;
					p_usb_noti.attach = CCIC_NOTIFY_ATTACH;
					p_usb_noti.drp = USB_STATUS_NOTIFY_ATTACH_UFP;
					p_usb_noti.sub3 = 0;
					p_usb_noti.pd = NULL;
					manager_notifier_notify(&p_usb_noti);
					typec_manager.ccic_drp_state = USB_STATUS_NOTIFY_ATTACH_UFP;
				}
				break;
			case MUIC_NOTIFY_CMD_DETACH:
				typec_manager.muic_attach_state_without_ccic = 0;
				p_usb_noti.src = CCIC_NOTIFY_DEV_MUIC;
				p_usb_noti.dest = CCIC_NOTIFY_DEV_USB;
				p_usb_noti.id = CCIC_NOTIFY_ID_USB;
				p_usb_noti.attach = CCIC_NOTIFY_DETACH;
				p_usb_noti.drp = USB_STATUS_NOTIFY_DETACH;
				p_usb_noti.sub3 = 0;
				p_usb_noti.pd = NULL;
				manager_notifier_notify(&p_usb_noti);
				typec_manager.ccic_drp_state = USB_STATUS_NOTIFY_DETACH;
				break;
			default :
				break;
		}
	}
}

void water_dry_time_update(int mode)
{
	struct timeval time;
	struct rtc_time det_time;
	static int rtc_update_check = 1;

	do_gettimeofday(&time);
	if (rtc_update_check) {
		rtc_update_check = 0;
		rtc_time_to_tm(time.tv_sec, &det_time);
		pr_info("%s: year=%d\n", __func__,  det_time.tm_year);
		if (det_time.tm_year == 70) { /* (1970-01-01 00:00:00) */
			queue_delayed_work(system_power_efficient_wq, &typec_manager.rtctime_update_work, msecs_to_jiffies(5000));
		}
	}

	if (mode) {
		/* WATER */
		typec_manager.waterDet_time = time.tv_sec;
	} else {
		/* DRY */
		typec_manager.dryDet_time = time.tv_sec;
		calc_duration_time(typec_manager.waterDet_time,
			typec_manager.dryDet_time, &typec_manager.waterDet_duration);
	}
}

static void water_det_rtc_time_update(struct work_struct *work)
{
	struct timeval time;
	struct rtc_time rtctime;
	static int max_retry = 1;

	do_gettimeofday(&time);
	rtc_time_to_tm(time.tv_sec, &rtctime);
	if ((rtctime.tm_year == 70) && (max_retry<5)) {
		/* (1970-01-01 00:00:00) */
		if (typec_manager.wVbus_det) {
			calc_duration_time(typec_manager.wVbusHigh_time,
				time.tv_sec, &typec_manager.wVbus_duration);
			typec_manager.wVbusHigh_time = time.tv_sec;
		}
		max_retry++;
		queue_delayed_work(system_power_efficient_wq, &typec_manager.rtctime_update_work, msecs_to_jiffies(5000));
	} else {
		if (typec_manager.water_det) {
			typec_manager.waterDet_time = time.tv_sec;
			typec_manager.waterDet_duration += max_retry*5;
			if (typec_manager.wVbus_det) {
				typec_manager.wVbusHigh_time = time.tv_sec;
				typec_manager.wVbus_duration += 5;
			}
		}
	}
}

void wVbus_time_update(int mode)
{
	struct timeval time;

	do_gettimeofday(&time);

	if (mode) {
		/* WVBUS HIGH */
		typec_manager.wVbusHigh_time = time.tv_sec;
	} else {
		/* WVBUS LOW */
		typec_manager.wVbusLow_time = time.tv_sec;
		calc_duration_time(typec_manager.wVbusHigh_time,
			typec_manager.wVbusLow_time, &typec_manager.wVbus_duration);
	}
}

#if defined(CONFIG_VBUS_NOTIFIER)
void handle_muic_fake_event(int event)
{

	if (typec_manager.muic_fake_event_wq_processing) {
		typec_manager.muic_fake_event_wq_processing = 0;
		cancel_delayed_work_sync(&typec_manager.vbus_noti_work);
	}

	switch (event) {
		case EVENT_CANCEL:
			typec_manager.muic_attach_state_without_ccic = 0;
			break;
		case EVENT_LOAD:
			if(typec_manager.muic_attach_state_without_ccic
				|| typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_UFP) {
				queue_delayed_work(system_power_efficient_wq, &typec_manager.vbus_noti_work, msecs_to_jiffies(1000));
				typec_manager.muic_fake_event_wq_processing = 1;
			}
			break;
		default :
			break;
	}
}


static void muic_fake_event_work(struct work_struct *work)
{
	CC_NOTI_ATTACH_TYPEDEF muic_noti;

	pr_info("usb: [M] %s: drp=%d, rid=%d, without_ccic=%d\n", __func__,
		typec_manager.ccic_drp_state,
		typec_manager.ccic_rid_state,
		typec_manager.muic_attach_state_without_ccic);

	typec_manager.muic_fake_event_wq_processing = 0;

	if( typec_manager.ccic_rid_state == RID_523K ||  typec_manager.ccic_rid_state == RID_619K
		|| typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_DFP
		|| typec_manager.vbus_state == STATUS_VBUS_HIGH) {
		return;
	} else if (typec_manager.muic_action == MUIC_NOTIFY_CMD_DETACH) {
		typec_manager.muic_attach_state_without_ccic = 1;
		queue_delayed_work(system_power_efficient_wq, &typec_manager.muic_noti_work, msecs_to_jiffies(0));
		return;
	}

	typec_manager.muic_attach_state_without_ccic = 1;
	muic_noti.src = CCIC_NOTIFY_DEV_MANAGER;
	muic_noti.dest = CCIC_NOTIFY_DEV_MUIC;
	muic_noti.id = CCIC_NOTIFY_ID_ATTACH;
	muic_noti.attach = 0;
	muic_noti.rprd = 0;
	muic_noti.cable_type = typec_manager.muic_cable_type;
	muic_noti.pd = NULL;
	manager_notifier_notify(&muic_noti);
}
#endif

#if defined(CONFIG_CCIC_NOTIFIER)
static int manager_handle_ccic_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
	MANAGER_NOTI_TYPEDEF p_noti = *(MANAGER_NOTI_TYPEDEF *)data;
	CC_NOTI_ATTACH_TYPEDEF bat_noti;
	CC_NOTI_ATTACH_TYPEDEF muic_noti;
	int ret = 0;

	pr_info("usb: [M] %s: src:%s dest:%s id:%s attach/rid:%d\n", __func__,
		(p_noti.src<CCIC_NOTI_DEST_NUM)? CCIC_NOTI_DEST_Print[p_noti.src]:"unknown",
		(p_noti.dest<CCIC_NOTI_DEST_NUM)? CCIC_NOTI_DEST_Print[p_noti.dest]:"unknown",
		(p_noti.id<CCIC_NOTI_ID_NUM)? CCIC_NOTI_ID_Print[p_noti.id]:"unknown",
		p_noti.sub1);

#if defined(CONFIG_VBUS_NOTIFIER)
	handle_muic_fake_event(EVENT_CANCEL);
#endif

	switch (p_noti.id) {
	case CCIC_NOTIFY_ID_POWER_STATUS:
		if(p_noti.sub1) { /*attach*/
			typec_manager.pd_con_state = 1;	// PDIC_NOTIFY_EVENT_PD_SINK
			if( (typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_UFP) &&
				!typec_manager.is_UFPS){
				pr_info("usb: [M] %s: PD charger + UFP\n", __func__);
				cable_type_check_work(true, 60);
			}
		}
		p_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
		if(typec_manager.pd == NULL)
			typec_manager.pd = p_noti.pd;
		break;
	case CCIC_NOTIFY_ID_ATTACH:		// for MUIC
			if (typec_manager.ccic_attach_state != p_noti.sub1) {
				typec_manager.ccic_attach_state = p_noti.sub1;
				typec_manager.muic_data_refresh = 0;
				typec_manager.is_UFPS = 0;
				if(typec_manager.ccic_attach_state == CCIC_NOTIFY_ATTACH){
					pr_info("usb: [M] %s: CCIC_NOTIFY_ATTACH\n", __func__);
					typec_manager.water_det = 0;
					typec_manager.pd_con_state = 0;
				}
			}

			if (typec_manager.ccic_attach_state == CCIC_NOTIFY_DETACH) {
				pr_info("usb: [M] %s: CCIC_NOTIFY_DETACH (pd=%d, cable_type=%d)\n", __func__,
					typec_manager.pd_con_state, typec_manager.cable_type);
				cable_type_check_work(false, 0);
				if (typec_manager.pd_con_state) {
					typec_manager.pd_con_state = 0;
					bat_noti.src = CCIC_NOTIFY_DEV_CCIC;
					bat_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
					bat_noti.id = CCIC_NOTIFY_ID_ATTACH;
					bat_noti.attach = CCIC_NOTIFY_DETACH;
					bat_noti.rprd = 0;
					bat_noti.cable_type = ATTACHED_DEV_UNOFFICIAL_ID_ANY_MUIC; // temp
					bat_noti.pd = NULL;
					manager_notifier_notify(&bat_noti);
				}
			}
		break;
	case CCIC_NOTIFY_ID_RID:	// for MUIC (FAC)
		typec_manager.ccic_rid_state = p_noti.sub1;
		break;
	case CCIC_NOTIFY_ID_USB:	// for USB3
		if ((typec_manager.cable_type == MANAGER_NOTIFY_MUIC_CHARGER)
			|| (p_noti.sub2 != USB_STATUS_NOTIFY_DETACH && /*drp */
			(typec_manager.ccic_rid_state == RID_523K || typec_manager.ccic_rid_state == RID_619K))) {
			return 0;
		}
		if ((typec_manager.cable_type == MANAGER_NOTIFY_MUIC_TIMEOUT_OPEN_DEVICE) 
			&&  (p_noti.sub2 == USB_STATUS_NOTIFY_ATTACH_UFP) ) {
				pr_info("usb: [M] %s: DCD Timeout case.\n", __func__);
				cable_type_check_work(false, 0);
		} else if (p_noti.sub2 == USB_STATUS_NOTIFY_DETACH)
			cable_type_check_work(false, 0);
		else
			;
		break;
	case CCIC_NOTIFY_ID_WATER:
		if (p_noti.sub1) {	/* attach */
			if(!typec_manager.water_det) {
					typec_manager.water_det = 1;
					typec_manager.water_count++;

					muic_noti.src = CCIC_NOTIFY_DEV_CCIC;
					muic_noti.dest = CCIC_NOTIFY_DEV_MUIC;
					muic_noti.id = CCIC_NOTIFY_ID_WATER;
					muic_noti.attach = CCIC_NOTIFY_ATTACH;
					muic_noti.rprd = 0;
					muic_noti.cable_type = 0;
					muic_noti.pd = NULL;
					manager_notifier_notify(&muic_noti);

					/*update water time */
					water_dry_time_update((int)p_noti.sub1);

					if (typec_manager.vbus_state == STATUS_VBUS_HIGH) {
						p_noti.sub3 = typec_manager.water_cable_type; /* cable_type */
					} else {
						/* If the cable is not connected, skip the battery event. */
						return 0;
					}
			} else {
				/* Ignore duplicate events */
				return 0;
			}
		} else {
			typec_manager.water_det = 0;
			typec_manager.dry_count++;

			muic_noti.src = CCIC_NOTIFY_DEV_CCIC;
			muic_noti.dest = CCIC_NOTIFY_DEV_MUIC;
			muic_noti.id = CCIC_NOTIFY_ID_WATER;
			muic_noti.attach = CCIC_NOTIFY_DETACH;
			muic_noti.rprd = 0;
			muic_noti.cable_type = 0;
			muic_noti.pd = NULL;
			manager_notifier_notify(&muic_noti);

			/* update run_dry time */
			water_dry_time_update((int)p_noti.sub1);

			if (typec_manager.wVbus_det)
				p_noti.sub3 = typec_manager.water_cable_type;
			else
				return 0;
		}
		break;
	default:
		break;
	}

	ret = manager_notifier_notify(&p_noti);

	return ret;
}
#endif

static void manager_set_alternate_mode(int listener)
{
	pccic_data_t pccic_data = NULL;

	pr_info("%s : listener %d\n", __func__, listener);

#if defined(CONFIG_CCIC_NOTIFIER)
	pccic_data = dev_get_drvdata(ccic_device);
#endif
  	if (!pccic_data) {
		pr_err("there is no set_enable_alternate_mode\n");
		return;
	}
	if (!pccic_data->set_enable_alternate_mode) {
		pr_err("there is no set_enable_alternate_mode\n");
		return;
	}

	if (listener == MANAGER_NOTIFY_CCIC_BATTERY)
		typec_manager.alt_is_support |= CCIC_BATTERY;
	else if (listener == MANAGER_NOTIFY_CCIC_USB) 
		typec_manager.alt_is_support |= CCIC_USB;
	else if (listener == MANAGER_NOTIFY_CCIC_DP)
		typec_manager.alt_is_support |= CCIC_DP;
	else 
		pr_info("no support driver to start alternate mode\n");

	if (typec_manager.dp_is_support) {
		if (typec_manager.alt_is_support == (CCIC_DP | CCIC_USB | CCIC_BATTERY))
			pccic_data->set_enable_alternate_mode(ALTERNATE_MODE_READY | ALTERNATE_MODE_START);
	} else {
		if (typec_manager.alt_is_support == (CCIC_USB | CCIC_BATTERY))
			pccic_data->set_enable_alternate_mode(ALTERNATE_MODE_READY | ALTERNATE_MODE_START);
	}
}

#if defined(CONFIG_MUIC_NOTIFIER)
static int manager_handle_muic_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
	CC_NOTI_ATTACH_TYPEDEF p_noti = *(CC_NOTI_ATTACH_TYPEDEF *)data;
	CC_NOTI_USB_STATUS_TYPEDEF usb_noti;

	pr_info("usb: [M] %s: attach:%d, cable_type:%d\n", __func__,
		p_noti.attach, p_noti.cable_type);

	typec_manager.muic_action = p_noti.attach;
	typec_manager.muic_cable_type = p_noti.cable_type;
	typec_manager.muic_data_refresh = 1;

	if(typec_manager.water_det){
		/* If Water det irq case is ignored */
		if(p_noti.attach) typec_manager.muic_attach_state_without_ccic = 1;
		pr_info("usb: [M] %s: Water detected case\n", __func__);
		return 0;
	}

	if (p_noti.attach &&  typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_DETACH) {
		if(typec_manager.ccic_rid_state != RID_523K &&  typec_manager.ccic_rid_state != RID_619K)
		typec_manager.muic_attach_state_without_ccic = 1;
	}

	switch (p_noti.cable_type) {
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_USB_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_CDP_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		pr_info("usb: [M] %s: USB(%d) %s, CCIC: %s \n", __func__,
			p_noti.cable_type, p_noti.attach ? "Attached": "Detached",
			typec_manager.ccic_attach_state? "Attached": "Detached");

		if(typec_manager.muic_action) {
			typec_manager.cable_type = MANAGER_NOTIFY_MUIC_USB;
		}

		if(typec_manager.muic_attach_state_without_ccic) {
			if (p_noti.attach) {
				queue_delayed_work(system_power_efficient_wq, &typec_manager.muic_noti_work, msecs_to_jiffies(2000));
			} else {
				queue_delayed_work(system_power_efficient_wq, &typec_manager.muic_noti_work, 0);
			}
		}
		break;

	case ATTACHED_DEV_TA_MUIC:
		pr_info("usb: [M] %s: TA(%d) %s \n", __func__, p_noti.cable_type,
			p_noti.attach ? "Attached": "Detached");

		if(typec_manager.muic_action) {
			typec_manager.cable_type = MANAGER_NOTIFY_MUIC_CHARGER;
		}

		if(p_noti.attach && typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_UFP ) {
			if(typec_manager.pd_con_state) {
				cable_type_check_work(false, 0);
			}
			/* Turn off the USB Phy when connected to the charger */
			usb_noti.src = CCIC_NOTIFY_DEV_MUIC;
			usb_noti.dest = CCIC_NOTIFY_DEV_USB;
			usb_noti.id = CCIC_NOTIFY_ID_USB;
			usb_noti.attach = CCIC_NOTIFY_DETACH;
			usb_noti.drp = USB_STATUS_NOTIFY_DETACH;
			usb_noti.sub3 = 0;
			usb_noti.pd = NULL;
			manager_notifier_notify(&usb_noti);
		}
		break;

	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
		pr_info("usb: [M] %s: AFC or QC Prepare(%d) %s \n", __func__,
			p_noti.cable_type, p_noti.attach ? "Attached": "Detached");
		break;

	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
		pr_info("usb: [M] %s: DCD Timeout device is detected(%d) %s \n",
			__func__, p_noti.cable_type,
			p_noti.attach ? "Attached": "Detached");

		if(typec_manager.muic_action) {
			typec_manager.cable_type = MANAGER_NOTIFY_MUIC_TIMEOUT_OPEN_DEVICE;
			if(typec_manager.ccic_drp_state == USB_STATUS_NOTIFY_ATTACH_UFP) {
				pr_info("usb: [M] %s: DCD Timeout case schedule work enable_state[%d]\n", 
					__func__, typec_manager.usb_enable_state);
				cable_type_check_work(true, 10);
			}
		}
		break;

	default:
		pr_info("usb: [M] %s: Cable(%d) %s \n", __func__, p_noti.cable_type,
			p_noti.attach ? "Attached": "Detached");
		break;
	}
	if(!typec_manager.muic_action) {
		typec_manager.cable_type = MANAGER_NOTIFY_MUIC_NONE;
	}

	if (!(p_noti.attach) && typec_manager.ccic_attach_state && typec_manager.pd_con_state) {
		/* If PD charger + detach case is ignored */
		pr_info("usb: [M] %s: PD charger detached case\n", __func__);
	} else {
		p_noti.src = CCIC_NOTIFY_DEV_MUIC;
		p_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
		manager_notifier_notify(&p_noti);
	}

	return 0;
}
#endif

#if defined(CONFIG_VBUS_NOTIFIER)
static int manager_handle_vbus_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
	vbus_status_t vbus_type = *(vbus_status_t *)data;
	CC_NOTI_ATTACH_TYPEDEF bat_noti;

	pr_info("usb: [M] %s: cmd=%lu, vbus_type=%s, WATER DET=%d ATTACH=%s (%d)\n", __func__,
		action, vbus_type == STATUS_VBUS_HIGH ? "HIGH" : "LOW", typec_manager.water_det,
		typec_manager.ccic_attach_state == CCIC_NOTIFY_ATTACH ? "ATTACH":"DETACH",
		typec_manager.muic_attach_state_without_ccic);

	typec_manager.vbus_state = vbus_type;

	switch (vbus_type) {
	case STATUS_VBUS_HIGH:
		if (typec_manager.water_det) {
			bat_noti.src = CCIC_NOTIFY_DEV_CCIC;
			bat_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
			bat_noti.id = CCIC_NOTIFY_ID_WATER;
			bat_noti.attach = CCIC_NOTIFY_ATTACH;
			bat_noti.rprd = 0;
			bat_noti.cable_type = typec_manager.water_cable_type;
			bat_noti.pd = NULL;
			manager_notifier_notify(&bat_noti);
		}
		break;
	case STATUS_VBUS_LOW:
		if (typec_manager.water_det) {
			bat_noti.src = CCIC_NOTIFY_DEV_CCIC;
			bat_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
			bat_noti.id = CCIC_NOTIFY_ID_ATTACH;
			bat_noti.attach = CCIC_NOTIFY_DETACH;
			bat_noti.rprd = 0;
			bat_noti.cable_type = typec_manager.water_cable_type;
			bat_noti.pd = NULL;
			manager_notifier_notify(&bat_noti);
		}
		handle_muic_fake_event(EVENT_LOAD);
		break;
	default:
		break;
	}

	return 0;
}
#endif

int manager_notifier_register(struct notifier_block *nb, notifier_fn_t notifier,
			manager_notifier_device_t listener)
{
	int ret = 0;
	MANAGER_NOTI_TYPEDEF m_noti;

	pr_info("usb: [M] %s: listener=%d register\n", __func__, listener);
	if(!manager_notifier_init_done)
		manager_notifier_init();

#if defined(CONFIG_CCIC_NOTIFIER)
	ccic_notifier_init();
#endif

	/* Check if MANAGER Notifier is ready. */
	if (!manager_device) {
		pr_err("usb: [M] %s: Not Initialized...\n", __func__);
		return -1;
	}

	if (listener == MANAGER_NOTIFY_CCIC_MUIC) {
		SET_MANAGER_NOTIFIER_BLOCK(nb, notifier, listener);
		ret = blocking_notifier_chain_register(&(typec_manager.manager_muic_notifier), nb);
		if (ret < 0)
			pr_err("usb: [M] %s: muic blocking_notifier_chain_register error(%d)\n",
					__func__, ret);
	} else {
		SET_MANAGER_NOTIFIER_BLOCK(nb, notifier, listener);
		ret = blocking_notifier_chain_register(&(typec_manager.manager_ccic_notifier), nb);
		if (ret < 0)
			pr_err("usb: [M] %s: ccic blocking_notifier_chain_register error(%d)\n",
					__func__, ret);
	}

		/* current manager's attached_device status notify */
	if(listener == MANAGER_NOTIFY_CCIC_BATTERY) {
		/* CC_NOTI_ATTACH_TYPEDEF */
		m_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		m_noti.dest = CCIC_NOTIFY_DEV_BATTERY;
		m_noti.sub1 = (typec_manager.ccic_attach_state || typec_manager.muic_action);
		m_noti.sub2 = 0;
		m_noti.sub3 = 0;
		m_noti.pd = typec_manager.pd;
		if(typec_manager.water_det && m_noti.sub1) {
			m_noti.id = CCIC_NOTIFY_ID_WATER;
			m_noti.sub3 = typec_manager.water_cable_type;
		} else {
			m_noti.id = CCIC_NOTIFY_ID_ATTACH;
			if(typec_manager.pd_con_state) {
				pr_info("usb: [M] %s: PD is attached already\n", __func__);
				m_noti.id = CCIC_NOTIFY_ID_POWER_STATUS;
			} else if(typec_manager.muic_cable_type != ATTACHED_DEV_NONE_MUIC) {
				m_noti.sub3= typec_manager.muic_cable_type;
			} else {
				switch(typec_manager.ccic_drp_state){
					case USB_STATUS_NOTIFY_ATTACH_UFP:
						m_noti.sub3 = ATTACHED_DEV_USB_MUIC;
						break;
					case USB_STATUS_NOTIFY_ATTACH_DFP:
						m_noti.sub3 = ATTACHED_DEV_OTG_MUIC;
						break;
					default:
						m_noti.sub3 = ATTACHED_DEV_NONE_MUIC;
						break;
				}
			}
		}
		pr_info("usb: [M] %s BATTERY: cable_type=%d (%s) \n", __func__, m_noti.sub3,
			typec_manager.muic_cable_type? "MUIC" : "CCIC");
		nb->notifier_call(nb, m_noti.id, &(m_noti));
		manager_set_alternate_mode(listener);
	} else if(listener == MANAGER_NOTIFY_CCIC_USB) {
		/* CC_NOTI_USB_STATUS_TYPEDEF */
		m_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		m_noti.dest = CCIC_NOTIFY_DEV_USB;
		m_noti.id = CCIC_NOTIFY_ID_USB;
		if (typec_manager.water_det)
			m_noti.sub1 = 0;
		else
			m_noti.sub1 = typec_manager.ccic_attach_state || typec_manager.muic_action;

		if (m_noti.sub1) {
			if (typec_manager.ccic_drp_state &&
				(typec_manager.cable_type != MANAGER_NOTIFY_MUIC_CHARGER)) {
				m_noti.sub2 = typec_manager.ccic_drp_state;
			} else if (typec_manager.cable_type == MANAGER_NOTIFY_MUIC_USB) {
				typec_manager.ccic_drp_state = USB_STATUS_NOTIFY_ATTACH_UFP;
				m_noti.sub2 = USB_STATUS_NOTIFY_ATTACH_UFP;
			} else {
				typec_manager.ccic_drp_state = USB_STATUS_NOTIFY_DETACH;
				m_noti.sub2 = USB_STATUS_NOTIFY_DETACH;
			}
		} else {
				m_noti.sub2 = USB_STATUS_NOTIFY_DETACH;
		}
		m_noti.sub3 = 0;
		pr_info("usb: [M] %s USB: attach=%d, drp=%s \n", __func__,	m_noti.sub1,
			CCIC_NOTI_USB_STATUS_Print[m_noti.sub2]);
		nb->notifier_call(nb, m_noti.id, &(m_noti));
		manager_set_alternate_mode(listener);
	} else if(listener == MANAGER_NOTIFY_CCIC_DP) {
		m_noti.src = CCIC_NOTIFY_DEV_MANAGER;
		m_noti.dest = CCIC_NOTIFY_DEV_DP;
		m_noti.sub2 = 0;
		m_noti.sub3 = 0;
		if (typec_manager.dp_attach_state == CCIC_NOTIFY_ATTACH) {
			m_noti.id = CCIC_NOTIFY_ID_DP_CONNECT;
			m_noti.sub1 = typec_manager.dp_attach_state;
			m_noti.sub2 = typec_manager.dp_vid;
			m_noti.sub3 = typec_manager.dp_pid;
			pr_info("usb: [M] %s DP_CONNECT sub1 %d, 0x%x 0x%x\n", __func__,
					m_noti.sub1, m_noti.sub2, m_noti.sub3);
			nb->notifier_call(nb, m_noti.id, &(m_noti));

			m_noti.id = CCIC_NOTIFY_ID_DP_LINK_CONF;
			m_noti.sub1 = typec_manager.dp_cable_type;
			nb->notifier_call(nb, m_noti.id, &(m_noti));

			if (typec_manager.dp_hpd_state == CCIC_NOTIFY_HIGH) {
				m_noti.id = CCIC_NOTIFY_ID_DP_HPD;
				m_noti.sub1 = typec_manager.dp_hpd_state;
				nb->notifier_call(nb, m_noti.id, &(m_noti));
			}
		}
		manager_set_alternate_mode(listener);
	}

	return ret;
}

int manager_notifier_unregister(struct notifier_block *nb)
{
	int ret = 0;
	pr_info("usb: [M] %s: listener=%d unregister\n", __func__, nb->priority);

	if (nb->priority == MANAGER_NOTIFY_CCIC_MUIC) {
		ret = blocking_notifier_chain_unregister(&(typec_manager.manager_muic_notifier), nb);
		if (ret < 0)
			pr_err("usb: [M] %s: muic blocking_notifier_chain_unregister error(%d)\n",
					__func__, ret);
		DESTROY_MANAGER_NOTIFIER_BLOCK(nb);
	} else {
		ret = blocking_notifier_chain_unregister(&(typec_manager.manager_ccic_notifier), nb);
		if (ret < 0)
			pr_err("usb: [M] %s: ccic blocking_notifier_chain_unregister error(%d)\n",
					__func__, ret);
		DESTROY_MANAGER_NOTIFIER_BLOCK(nb);
	}
	return ret;
}

static void delayed_manger_notifier_init(struct work_struct *work)
{
	int ret = 0;
	int notifier_result = 0;
	static int retry_count = 1;
	int max_retry_count = 5;

	pr_info("%s : %d = times!\n",__func__,retry_count);
#if defined(CONFIG_VBUS_NOTIFIER)
	if(confirm_manager_notifier_register & (1 << VBUS_NOTIFIER))
	{
		ret = vbus_notifier_register(&typec_manager.vbus_nb, manager_handle_vbus_notification,VBUS_NOTIFY_DEV_MANAGER);
		if(ret)
			notifier_result |= (1 << VBUS_NOTIFIER);
	}
#endif
#if defined(CONFIG_CCIC_NOTIFIER)
	if(confirm_manager_notifier_register & (1 << CCIC_NOTIFIER))
	{
		ret = ccic_notifier_register(&typec_manager.ccic_nb, manager_handle_ccic_notification,CCIC_NOTIFY_DEV_MANAGER);
		if(ret)
			notifier_result |= (1 << CCIC_NOTIFIER);
	}
#endif
#if defined(CONFIG_MUIC_NOTIFIER)
	if(confirm_manager_notifier_register & (1 << MUIC_NOTIFIER))
	{
		ret = muic_notifier_register(&typec_manager.muic_nb, manager_handle_muic_notification,MUIC_NOTIFY_DEV_MANAGER);
		if(ret)
			notifier_result |= (1 << MUIC_NOTIFIER);
	}
#endif

	confirm_manager_notifier_register = notifier_result;
	pr_info("%s : result of register = %d!\n",__func__, confirm_manager_notifier_register);

	if(confirm_manager_notifier_register)
	{
		pr_err("Manager notifier init time is %d.\n",retry_count);
		if(retry_count++ != max_retry_count)
			queue_delayed_work(system_power_efficient_wq, &typec_manager.manager_init_work, msecs_to_jiffies(2000));
		else
			pr_err("fail to init manager notifier\n");
	}
	else
	{
		pr_info("%s : done!\n",__func__);
	}
}


static int manager_notifier_init(void)
{
	int ret = 0;
	int notifier_result = 0;
	pccic_data_t pccic_data = NULL;
	struct device_node *np = NULL;

	pr_info("usb: [M] %s\n", __func__);
#if defined(CONFIG_CCIC_NOTIFIER)
	ccic_notifier_init();
	pccic_data = dev_get_drvdata(ccic_device);
#endif

	if(manager_notifier_init_done)
	{
		pr_err("%s already registered\n", __func__);
		goto out;
	}
	manager_notifier_init_done = 1;

	manager_device = sec_device_create(NULL, "typec_manager");
	if (IS_ERR(manager_device)) {
		pr_err("usb: [M] %s Failed to create device(switch)!\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	typec_manager.ccic_attach_state = CCIC_NOTIFY_DETACH;
	typec_manager.ccic_drp_state = USB_STATUS_NOTIFY_DETACH;
	typec_manager.muic_action = MUIC_NOTIFY_CMD_DETACH;
	typec_manager.muic_cable_type = ATTACHED_DEV_NONE_MUIC;
	typec_manager.cable_type = MANAGER_NOTIFY_MUIC_NONE;
	typec_manager.muic_data_refresh = 0;
	typec_manager.usb_enum_state = 0;
	typec_manager.water_det = 0;
	typec_manager.wVbus_det = 0;
	typec_manager.water_count =0;
	typec_manager.dry_count = 0;
	typec_manager.usb210_count = 0;
	typec_manager.usb310_count = 0;
	typec_manager.waterChg_count = 0;
	typec_manager.waterDet_duration = 0;
	typec_manager.wVbus_duration = 0;
	typec_manager.alt_is_support = 0;
	typec_manager.dp_is_support = 0;
	typec_manager.dp_is_connect = 0;
	typec_manager.dp_hs_connect = 0;
	typec_manager.dp_check_done = 1;
	usb_external_notify_register(&typec_manager.manager_external_notifier_nb,
		manager_external_notifier_notification, EXTERNAL_NOTIFY_DEV_MANAGER);
	typec_manager.muic_attach_state_without_ccic = 0;
#if defined(CONFIG_VBUS_NOTIFIER)
	typec_manager.muic_fake_event_wq_processing = 0;
#endif
	typec_manager.vbus_state = 0;
	typec_manager.is_UFPS = 0;
	typec_manager.ccic_rid_state = RID_UNDEFINED;
	typec_manager.pd = NULL;
#if defined(CONFIG_HICCUP_CHARGER) && defined(CONFIG_BATTERY_SAMSUNG)
	typec_manager.water_cable_type = lpcharge ?
		ATTACHED_DEV_UNDEFINED_RANGE_MUIC :
		ATTACHED_DEV_HICCUP_MUIC;
#else
	typec_manager.water_cable_type = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
#endif

	BLOCKING_INIT_NOTIFIER_HEAD(&(typec_manager.manager_ccic_notifier));
	BLOCKING_INIT_NOTIFIER_HEAD(&(typec_manager.manager_muic_notifier));

	INIT_DELAYED_WORK(&typec_manager.manager_init_work,
		delayed_manger_notifier_init);

	INIT_DELAYED_WORK(&typec_manager.cable_check_work,
		cable_type_check);

	INIT_DELAYED_WORK(&typec_manager.muic_noti_work,
		muic_work_without_ccic);

	INIT_DELAYED_WORK(&typec_manager.rtctime_update_work,
		water_det_rtc_time_update);

	if (pccic_data && pccic_data->set_enable_alternate_mode)
		pccic_data->set_enable_alternate_mode(ALTERNATE_MODE_NOT_READY);
#if defined(CONFIG_VBUS_NOTIFIER)
	INIT_DELAYED_WORK(&typec_manager.vbus_noti_work,
		muic_fake_event_work);
#endif

	// Register manager handler to ccic notifier block list
#if defined(CONFIG_VBUS_NOTIFIER)
	ret = vbus_notifier_register(&typec_manager.vbus_nb, manager_handle_vbus_notification,VBUS_NOTIFY_DEV_MANAGER);
	if(ret)
		notifier_result |= (1 << VBUS_NOTIFIER);
#endif
#if defined(CONFIG_CCIC_NOTIFIER)
	ret = ccic_notifier_register(&typec_manager.ccic_nb, manager_handle_ccic_notification,CCIC_NOTIFY_DEV_MANAGER);
	if(ret)
		notifier_result |= (1 << CCIC_NOTIFIER);
#endif
#if defined(CONFIG_MUIC_NOTIFIER)
	ret = muic_notifier_register(&typec_manager.muic_nb, manager_handle_muic_notification,MUIC_NOTIFY_DEV_MANAGER);
	if(ret)
		notifier_result |= (1 << MUIC_NOTIFIER);
#endif

	confirm_manager_notifier_register = notifier_result;
	pr_info("%s : result of register = %d!\n",__func__, confirm_manager_notifier_register);

	if(confirm_manager_notifier_register)
	{
		queue_delayed_work(system_power_efficient_wq, &typec_manager.manager_init_work, msecs_to_jiffies(2000));
	}
	else
	{
		pr_info("%s : done!\n",__func__);
	}
	
	np = of_find_node_by_name(NULL, "displayport");
	if (of_device_is_available(np) && !of_get_property(np, "dp,displayport_not_support", NULL)) {
		pr_info("%s: usb_host: support DP\n", __func__);
		typec_manager.dp_is_support = 1; 
	} else { 
		pr_info("%s: usb_host: no support DP\n", __func__);
		typec_manager.dp_is_support = 0;
	}

	pr_info("usb: [M] %s end\n", __func__);
out:
	return ret;
}

static void __exit manager_notifier_exit(void)
{
	pr_info("usb: [M] %s exit\n", __func__);
#if defined(CONFIG_VBUS_NOTIFIER)
	vbus_notifier_unregister(&typec_manager.vbus_nb);
#endif
#if defined(CONFIG_CCIC_NOTIFIER)
	ccic_notifier_unregister(&typec_manager.ccic_nb);
#endif
#if defined(CONFIG_MUIC_NOTIFIER)
	muic_notifier_unregister(&typec_manager.muic_nb);
#endif
	usb_external_notify_unregister(&typec_manager.manager_external_notifier_nb);
}

late_initcall(manager_notifier_init);
module_exit(manager_notifier_exit);

