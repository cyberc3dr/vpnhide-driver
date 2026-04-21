# vpnhide — kernel integration patches (Linux 4.9)

Reference: https://github.com/cyberc3dr/android_kernel_xiaomi_onclite/commit/9dc24a633f266cc47b4945c869fc31f4810bad2c

Apply these diffs to your 4.9 kernel source after running `setup.sh`.

```
curl -LSs "https://raw.githubusercontent.com/cyberc3dr/vpnhide-driver/main/kernel/setup.sh" | bash
```

Hook logic per file:
- `net/core/dev_ioctl.c`  — skip VPN devs in `dev_ifconf` loop; hide by name in `dev_ioctl`
- `net/core/rtnetlink.c`  — early return in `rtnl_fill_ifinfo`
- `net/ipv6/addrconf.c`   — early return in `inet6_fill_ifaddr`
- `net/ipv4/devinet.c`    — early return in `inet_fill_ifaddr`
- `net/ipv4/fib_trie.c`   — skip VPN entries in `fib_route_seq_show`

---

## net/core/dev_ioctl.c

Two hooks: `dev_ifconf` (SIOCGIFCONF) and `dev_ioctl` (all ioctls on VPN interfaces).

```diff
--- a/net/core/dev_ioctl.c
+++ b/net/core/dev_ioctl.c
@@ -6,6 +6,12 @@
 #include <linux/wireless.h>
 #include <net/wext.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 /*
  *	Map an interface index to its name (SIOCGIFNAME)
  */
@@ -90,6 +96,10 @@ static int dev_ifconf(struct net *net, char __user *arg)
 
 	total = 0;
 	for_each_netdev(net, dev) {
+#ifdef CONFIG_VPNHIDE
+		if (vpnhide_is_target_uid() && vpnhide_is_vpn_ifname(dev->name))
+			continue;
+#endif		
 		for (i = 0; i < NPROTO; i++) {
 			if (gifconf_list[i]) {
 				int done;
@@ -408,8 +418,24 @@ int dev_ioctl(struct net *net, unsigned int cmd, void __user *arg)
 		rtnl_unlock();
 		return ret;
 	}
+#ifdef CONFIG_VPNHIDE
+	if (cmd == SIOCGIFNAME) {
+		struct ifreq __kifr;
+		int __r = dev_ifname(net, (struct ifreq __user *)arg);
+		if (!__r && vpnhide_is_target_uid() &&
+		    !copy_from_user(&__kifr, arg, sizeof(__kifr)) &&
+		    vpnhide_is_vpn_ifname(__kifr.ifr_name)) {
+			if (vpnhide_debug_enabled)
+				pr_info("vpnhide: dev_ioctl: hiding SIOCGIFNAME iface=%s\n",
+				    __kifr.ifr_name);
+			return -ENODEV;
+		}
+		return __r;
+	}
+#else
 	if (cmd == SIOCGIFNAME)
 		return dev_ifname(net, (struct ifreq __user *)arg);
+#endif
 
 	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
 		return -EFAULT;
@@ -443,6 +469,15 @@ int dev_ioctl(struct net *net, unsigned int cmd, void __user *arg)
 		rcu_read_lock();
 		ret = dev_ifsioc_locked(net, &ifr, cmd);
 		rcu_read_unlock();
+#ifdef CONFIG_VPNHIDE
+		if (!ret && vpnhide_is_target_uid() &&
+		    vpnhide_is_vpn_ifname(ifr.ifr_name)) {
+			if (vpnhide_debug_enabled)
+				pr_info("vpnhide: dev_ioctl: hiding iface=%s cmd=0x%x\n",
+				    ifr.ifr_name, cmd);
+			return -ENODEV;
+		}
+#endif		
 		if (!ret) {
 			if (colon)
 				*colon = ':';
```

---

## net/core/rtnetlink.c

```diff
--- a/net/core/rtnetlink.c
+++ b/net/core/rtnetlink.c
@@ -57,6 +57,12 @@
 #include <net/rtnetlink.h>
 #include <net/net_namespace.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 struct rtnl_link {
 	rtnl_doit_func		doit;
 	rtnl_dumpit_func	dumpit;
@@ -1285,6 +1291,13 @@ static int rtnl_fill_ifinfo(struct sk_buff *skb, struct net_device *dev,
 			    int type, u32 pid, u32 seq, u32 change,
 			    unsigned int flags, u32 ext_filter_mask)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() && vpnhide_is_vpn_ifname(dev->name)) {
+		if (vpnhide_debug_enabled)
+			pr_info("vpnhide: rtnl_fill_ifinfo: hiding iface=%s\n", dev->name);
+		return -EMSGSIZE;
+	}
+#endif	
 	struct ifinfomsg *ifm;
 	struct nlmsghdr *nlh;
 	struct nlattr *af_spec;
```

---

## net/ipv6/addrconf.c

```diff
--- a/net/ipv6/addrconf.c
+++ b/net/ipv6/addrconf.c
@@ -93,6 +93,12 @@
 #include <linux/seq_file.h>
 #include <linux/export.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 /* Set to 3 to get tracing... */
 #define ACONF_DEBUG 2
 
@@ -4641,6 +4647,16 @@ static inline int inet6_ifaddr_msgsize(void)
 static int inet6_fill_ifaddr(struct sk_buff *skb, struct inet6_ifaddr *ifa,
 			     u32 portid, u32 seq, int event, unsigned int flags)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() &&
+	    ifa->idev && ifa->idev->dev &&
+	    vpnhide_is_vpn_ifname(ifa->idev->dev->name)) {
+		if (vpnhide_debug_enabled)
+			pr_info("vpnhide: inet6_fill_ifaddr: hiding iface=%s\n",
+			    ifa->idev->dev->name);
+		return 0;
+	}
+#endif	
 	struct nlmsghdr  *nlh;
 	u32 preferred, valid;
```

---

## net/ipv4/devinet.c

```diff
--- a/net/ipv4/devinet.c
+++ b/net/ipv4/devinet.c
@@ -67,6 +67,12 @@
 
 #include "fib_lookup.h"
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 #define IPV6ONLY_FLAGS	\
 		(IFA_F_NODAD | IFA_F_OPTIMISTIC | IFA_F_DADFAILED | \
 		 IFA_F_HOMEADDRESS | IFA_F_TENTATIVE | \
@@ -1538,6 +1544,16 @@ static int put_cacheinfo(struct sk_buff *skb, unsigned long cstamp,
 static int inet_fill_ifaddr(struct sk_buff *skb, struct in_ifaddr *ifa,
 			    u32 portid, u32 seq, int event, unsigned int flags)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() &&
+	    ifa->ifa_dev && ifa->ifa_dev->dev &&
+	    vpnhide_is_vpn_ifname(ifa->ifa_dev->dev->name)) {
+		if (vpnhide_debug_enabled)
+			pr_info("vpnhide: inet_fill_ifaddr: hiding iface=%s\n",
+			    ifa->ifa_dev->dev->name);
+		return 0;
+	}
+#endif	
 	struct ifaddrmsg *ifm;
 	struct nlmsghdr  *nlh;
 	u32 preferred, valid;
```

---

## net/ipv4/fib_trie.c

Key differences from 5.4:
- No `fib_nh_common` / `fib_info_nhc()` — device name is `fi->fib_dev->name` (macro: `fib_dev = fib_nh[0].nh_dev`)
- `if (fi)` has no braces — insert check **before** `if (fi)`, not inside it

```diff
--- a/net/ipv4/fib_trie.c
+++ b/net/ipv4/fib_trie.c
@@ -84,6 +84,12 @@
 #include <trace/events/fib.h>
 #include "fib_lookup.h"
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 static BLOCKING_NOTIFIER_HEAD(fib_chain);
 
 int register_fib_notifier(struct notifier_block *nb)
@@ -2630,6 +2636,17 @@ static int fib_route_seq_show(struct seq_file *seq, void *v)
 
 		seq_setwidth(seq, 127);
 
+#ifdef CONFIG_VPNHIDE
+		if (vpnhide_is_target_uid() &&
+		    fi && fi->fib_dev &&
+		    vpnhide_is_vpn_ifname(fi->fib_dev->name)) {
+			if (vpnhide_debug_enabled)
+				pr_info("vpnhide: fib_route_seq_show: hiding route for %s\n",
+				    fi->fib_dev->name);
+			continue;
+		}
+#endif		
+
 		if (fi)
 			seq_printf(seq,
 				   "%s\t%08X\t%08X\t%04X\t%d\t%u\t"
```