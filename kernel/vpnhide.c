// SPDX-License-Identifier: MIT

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/if.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/export.h>    /* added: EXPORT_SYMBOL_GPL */

#define MAX_TARGET_UIDS 64

static const char * const vpn_prefixes[] = {
	"tun", "ppp", "tap", "wg", "ipsec", "xfrm", "utun", "l2tp", "gre",
};

bool vpnhide_is_vpn_ifname(const char *name)
{
	int i;

	if (!name || !*name)
		return false;

	for (i = 0; i < ARRAY_SIZE(vpn_prefixes); i++) {
		if (strncmp(name, vpn_prefixes[i],
			    strlen(vpn_prefixes[i])) == 0)
			return true;
	}
	if (strstr(name, "vpn") || strstr(name, "VPN"))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(vpnhide_is_vpn_ifname);

static uid_t target_uids[MAX_TARGET_UIDS];
static int nr_target_uids;
static DEFINE_SPINLOCK(uids_lock);

bool vpnhide_is_target_uid(void)
{
	uid_t uid = from_kuid(&init_user_ns, current_uid());
	int i;

	if (READ_ONCE(nr_target_uids) == 0)
		return false;

	spin_lock(&uids_lock);
	for (i = 0; i < nr_target_uids; i++) {
		if (target_uids[i] == uid) {
			spin_unlock(&uids_lock);
			return true;
		}
	}
	spin_unlock(&uids_lock);
	return false;
}
EXPORT_SYMBOL_GPL(vpnhide_is_target_uid);

static ssize_t targets_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	char *buf, *line, *next;
	int new_count = 0;
	uid_t new_uids[MAX_TARGET_UIDS];

	if (count > PAGE_SIZE)
		return -EINVAL;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, count)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = '\0';

	for (line = buf; line && *line && new_count < MAX_TARGET_UIDS;
	     line = next) {
		unsigned long uid;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#')
			continue;

		if (kstrtoul(line, 10, &uid) == 0)
			new_uids[new_count++] = (uid_t)uid;
	}

	spin_lock(&uids_lock);
	memcpy(target_uids, new_uids, new_count * sizeof(uid_t));
	nr_target_uids = new_count;
	spin_unlock(&uids_lock);

	kfree(buf);
	pr_info("vpnhide: loaded %d target UIDs\n", new_count);
	return count;
}

static int targets_show(struct seq_file *m, void *v)
{
	int i;

	spin_lock(&uids_lock);
	for (i = 0; i < nr_target_uids; i++)
		seq_printf(m, "%u\n", target_uids[i]);
	spin_unlock(&uids_lock);
	return 0;
}

static int targets_open(struct inode *inode, struct file *file)
{
	return single_open(file, targets_show, NULL);
}

static const struct file_operations targets_fops = {
	.owner   = THIS_MODULE,
	.open    = targets_open,
	.read    = seq_read,
	.write   = targets_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static struct proc_dir_entry *targets_entry;

static int __init vpnhide_init(void)
{
	targets_entry = proc_create("vpnhide_targets", 0600, NULL,
				    &targets_fops); /* was: &targets_proc_ops */
	pr_info("vpnhide: loaded — write UIDs to /proc/vpnhide_targets\n");
	return 0;
}

late_initcall(vpnhide_init);
