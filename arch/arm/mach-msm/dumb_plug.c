/*
 * Author: Yatao Li aka v-yadli <glocklee@gmail.com>
 * Adapted from faux123's intelli_plug
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/rq_stats.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/cpufreq.h>

#include <linux/notifier.h> /* For definition of notifier_block*/
#include <linux/suspend.h>  /* For constants like PM_POST_SUSPEND */

#define INTELLI_PLUG_MAJOR_VERSION	2
#define INTELLI_PLUG_MINOR_VERSION	6

#define DEF_SAMPLING_MS			(1000)


static DEFINE_MUTEX(intelli_plug_mutex);
#define lock(x) \
    mutex_lock(&intelli_plug_mutex);\
    { x; } \
    mutex_unlock(&intelli_plug_mutex);

static struct delayed_work intelli_plug_work;
static struct delayed_work intelli_plug_boost;

static struct workqueue_struct *intelliplug_wq;
static struct workqueue_struct *intelliplug_boost_wq;

static unsigned int intelli_plug_active = 1;
module_param(intelli_plug_active, uint, 0644);

static unsigned int touch_boost_active = 1;
static int touch_boost_latch = 0;
static int wake_boost_latch = 0;

#define DEF_FREQ_MAX (2265000)
#define DEF_FREQ_MIN (300000)

static unsigned int freq_max = DEF_FREQ_MAX;
//static unsigned int freq_min = DEF_FREQ_MIN;
#define TOUCH_BOOST_DURATION 1500
#define WAKE_BOOST_DURATION 5000
module_param(touch_boost_active, uint, 0644);

//default to something sane rather than zero
static unsigned int sampling_time = DEF_SAMPLING_MS;

static void __cpuinit intelli_plug_boost_fn(struct work_struct *work)
{

	int nr_cpus = num_online_cpus();

	if (touch_boost_active)
    {
        lock(touch_boost_latch = TOUCH_BOOST_DURATION);
		if (nr_cpus < 2)
			cpu_up(1);
    }
}

static void __cpuinit intelli_plug_work_fn(struct work_struct *work)
{
	unsigned int cpu_count = 0;
	unsigned int nr_cpus = 0;

	int i;

	if (intelli_plug_active == 1) {
        //Dumb plug: em, sorry, we only have one cpu...
		cpu_count = 1;
        lock(
        if(touch_boost_latch > 0)
        {
            touch_boost_latch -= sampling_time;
            cpu_count++;
        }
        if(wake_boost_latch > 0)
        {
            wake_boost_latch -= sampling_time;
            pr_debug("dumb: wakeup = %d\n", wake_boost_latch);
            cpu_count = 4;
        }
        );

        if(cpu_count > 4)
            cpu_count = 4;

		nr_cpus = num_online_cpus();

        switch (cpu_count) {
            case 1:
                //take down everyone
                for (i = 3; i > 0; i--)
                    cpu_down(i);
                break;
            case 2:
                if (nr_cpus < 2) {
                    for (i = 1; i < cpu_count; i++)
                        cpu_up(i);
                } else {
                    for (i = 3; i >  1; i--)
                        cpu_down(i);
                }
                break;
            case 3:
                if (nr_cpus < 3) {
                    for (i = 1; i < cpu_count; i++)
                        cpu_up(i);
                } else {
                    for (i = 3; i > 2; i--)
                        cpu_down(i);
                }
                break;
            case 4:
                if (nr_cpus < 4)
                    for (i = 1; i < cpu_count; i++)
                        cpu_up(i);
                break;
            default:
                pr_err("Run Stat Error: Bad value %u\n", cpu_count);
                break;
        }
/*#ifdef DEBUG_INTELLI_PLUG
		else
			pr_info("intelli_plug is suspened!\n");
#endif
*/
	}
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(sampling_time));
}

static void wakeup_boost(void)
{
	unsigned int i, ret;
	struct cpufreq_policy policy;

	for_each_online_cpu(i) {

		ret = cpufreq_get_policy(&policy, i);
		if (ret)
			continue;

        freq_max = policy.max;
		policy.cur = policy.max = DEF_FREQ_MAX;
		cpufreq_update_policy(i);
	}
    lock(wake_boost_latch = WAKE_BOOST_DURATION);
}

static void __cpuinit intelli_plug_resume(void)
{
	int num_of_active_cores;
	int i;

	//mutex_lock(&intelli_plug_mutex);

	//mutex_unlock(&intelli_plug_mutex);

	/* wake up everyone */
    num_of_active_cores = num_possible_cpus();

	for (i = 1; i < num_of_active_cores; i++) {
		cpu_up(i);
	}

    pr_debug("dumb: wakeup\n");
	wakeup_boost();

	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(10));
}

extern int register_pm_notifier(struct notifier_block *);

static int __cpuinit pm_notifier_callback(struct notifier_block
        *self, unsigned long action, void *dev)
{
    pr_debug("dumb: pm_notifier_callback action=%lu\n",action);
    switch (action){
        case PM_POST_SUSPEND:
            intelli_plug_resume();
            break;
    }

    return NOTIFY_OK;
}


static void intelli_plug_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
#ifdef DEBUG_INTELLI_PLUG
	pr_info("intelli_plug touched!\n");
#endif
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_boost,
		msecs_to_jiffies(10));
}

static int intelli_plug_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "intelliplug";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;
	pr_info("%s found and connected!\n", dev->name);
	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void intelli_plug_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id intelli_plug_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			    BIT_MASK(ABS_MT_POSITION_X) |
			    BIT_MASK(ABS_MT_POSITION_Y) },
	}, /* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			    BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	}, /* touchpad */
	{ },
};

static struct input_handler intelli_plug_input_handler = {
	.event          = intelli_plug_input_event,
	.connect        = intelli_plug_input_connect,
	.disconnect     = intelli_plug_input_disconnect,
	.name           = "intelliplug_handler",
	.id_table       = intelli_plug_ids,
};

static struct notifier_block power_manage_driver = {
    .notifier_call  = pm_notifier_callback,
};


//static void screen_off_limit(bool on)
//{
//	unsigned int i, ret;
//	struct cpufreq_policy policy;
//	struct ip_cpu_info *l_ip_info;
//
//	/* not active, so exit */
//	if (screen_off_max == UINT_MAX)
//		return;
//
//	for_each_online_cpu(i) {
//
//		l_ip_info = &per_cpu(ip_info, i);
//		ret = cpufreq_get_policy(&policy, i);
//		if (ret)
//			continue;
//
//		if (on) {
//			/* save current instance */
//			l_ip_info->curr_max = policy.max;
//			policy.max = screen_off_max;
//		} else {
//			/* restore */
//			policy.max = l_ip_info->curr_max;
//		}
//		cpufreq_update_policy(i);
//	}
//}

int __init intelli_plug_init(void)
{
	int rc;

    return 0;

	//pr_info("intelli_plug: scheduler delay is: %d\n", delay);
	pr_info("dumb_plug: version %d.%d by v-yadli\n",
		 INTELLI_PLUG_MAJOR_VERSION,
		 INTELLI_PLUG_MINOR_VERSION);

	rc = input_register_handler(&intelli_plug_input_handler);

	intelliplug_wq = alloc_workqueue("intelliplug",
				WQ_HIGHPRI | WQ_UNBOUND, 1);
	intelliplug_boost_wq = alloc_workqueue("iplug_boost",
				WQ_HIGHPRI | WQ_UNBOUND, 1);
	INIT_DELAYED_WORK(&intelli_plug_work, intelli_plug_work_fn);
	INIT_DELAYED_WORK(&intelli_plug_boost, intelli_plug_boost_fn);

	//mutex_lock(&intelli_plug_mutex);
	//mutex_unlock(&intelli_plug_mutex);
    
    register_pm_notifier(&power_manage_driver);

	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(10));

	return 0;
}

MODULE_AUTHOR("Yatao Li <glocklee@gmail.com>");
MODULE_DESCRIPTION("'dumb_plug' - A dumb cpu hotplug driver for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

late_initcall(intelli_plug_init);
