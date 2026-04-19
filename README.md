# vpnhide — kernel integration patches

Apply these diffs to your 5.4 kernel source after running `setup.sh`.

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
@@ -7,6 +7,12 @@
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
@@ -71,6 +77,10 @@ int dev_ifconf(struct net *net, struct ifconf *ifc, int size)
 
 	total = 0;
 	for_each_netdev(net, dev) {
+#ifdef CONFIG_VPNHIDE
+		if (vpnhide_is_target_uid() && vpnhide_is_vpn_ifname(dev->name))
+			continue;
+#endif		
 		for (i = 0; i < NPROTO; i++) {
 			if (gifconf_list[i]) {
 				int done;
@@ -371,8 +381,22 @@ int dev_ioctl(struct net *net, unsigned int cmd, struct ifreq *ifr, bool *need_c
 
 	if (need_copyout)
 		*need_copyout = true;
+#ifdef CONFIG_VPNHIDE
+	if (cmd == SIOCGIFNAME) {
+		int __r = dev_ifname(net, ifr);
+		if (!__r && vpnhide_is_target_uid() &&
+		    vpnhide_is_vpn_ifname(ifr->ifr_name)) {
+			if(vpnhide_debug_enabled)
+				pr_info("vpnhide: dev_ioctl: hiding SIOCGIFNAME iface=%s\n",
+				    ifr->ifr_name);
+			return -ENODEV;
+		}
+		return __r;
+	}
+#else
 	if (cmd == SIOCGIFNAME)
 		return dev_ifname(net, ifr);
+#endif
 
 	ifr->ifr_name[IFNAMSIZ-1] = 0;
 
@@ -408,6 +432,15 @@ int dev_ioctl(struct net *net, unsigned int cmd, struct ifreq *ifr, bool *need_c
 		rcu_read_lock();
 		ret = dev_ifsioc_locked(net, ifr, cmd);
 		rcu_read_unlock();
+#ifdef CONFIG_VPNHIDE
+		if (!ret && vpnhide_is_target_uid() &&
+		    vpnhide_is_vpn_ifname(ifr->ifr_name)) {
+			if(vpnhide_debug_enabled)
+				pr_info("vpnhide: dev_ioctl: hiding iface=%s cmd=0x%x\n",
+				    ifr->ifr_name, cmd);
+			ret = -ENODEV;
+		}
+#endif		
 		if (colon)
 			*colon = ':';
 		return ret;
```
 
---
 
## net/core/rtnetlink.c
 
```diff
--- a/net/core/rtnetlink.c
+++ b/net/core/rtnetlink.c
@@ -54,6 +54,12 @@
 #include <net/rtnetlink.h>
 #include <net/net_namespace.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 #define RTNL_MAX_TYPE		50
 #define RTNL_SLAVE_MAX_TYPE	36
 
@@ -1597,6 +1603,13 @@ static int rtnl_fill_ifinfo(struct sk_buff *skb,
 			    u32 event, int *new_nsid, int new_ifindex,
 			    int tgt_netnsid, gfp_t gfp)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() && vpnhide_is_vpn_ifname(dev->name)) {
+		if(vpnhide_debug_enabled)
+			pr_info("vpnhide: rtnl_fill_ifinfo: hiding iface=%s\n", dev->name);
+		return -EMSGSIZE;
+	}
+#endif	
 	struct ifinfomsg *ifm;
 	struct nlmsghdr *nlh;
 	struct Qdisc *qdisc;
```
 
---
 
## net/ipv6/addrconf.c
 
```diff
--- a/net/ipv6/addrconf.c
+++ b/net/ipv6/addrconf.c
@@ -90,6 +90,12 @@
 #include <linux/seq_file.h>
 #include <linux/export.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 #define	INFINITY_LIFE_TIME	0xFFFFFFFF
 
 #define IPV6_MAX_STRLEN \
@@ -4972,6 +4978,16 @@ struct inet6_fill_args {
 static int inet6_fill_ifaddr(struct sk_buff *skb, struct inet6_ifaddr *ifa,
 			     struct inet6_fill_args *args)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() &&
+	    ifa->idev && ifa->idev->dev &&
+	    vpnhide_is_vpn_ifname(ifa->idev->dev->name)) {
+		if(vpnhide_debug_enabled)
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
@@ -62,6 +62,12 @@
 #include <net/net_namespace.h>
 #include <net/addrconf.h>
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 #define IPV6ONLY_FLAGS	\
 		(IFA_F_NODAD | IFA_F_OPTIMISTIC | IFA_F_DADFAILED | \
 		 IFA_F_HOMEADDRESS | IFA_F_TENTATIVE | \
@@ -1662,6 +1668,16 @@ static int put_cacheinfo(struct sk_buff *skb, unsigned long cstamp,
 static int inet_fill_ifaddr(struct sk_buff *skb, struct in_ifaddr *ifa,
 			    struct inet_fill_args *args)
 {
+#ifdef CONFIG_VPNHIDE
+	if (vpnhide_is_target_uid() &&
+	    ifa->ifa_dev && ifa->ifa_dev->dev &&
+	    vpnhide_is_vpn_ifname(ifa->ifa_dev->dev->name)) {
+		if(vpnhide_debug_enabled)
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
 
The route table entry is written via `seq_printf` inside `if (fi)`.
The device name is in `nhc->nhc_dev->name`. Skip before writing — no
seq buffer compaction needed.
 
```diff
--- a/net/ipv4/fib_trie.c
+++ b/net/ipv4/fib_trie.c
@@ -74,6 +74,12 @@
 #include <trace/events/fib.h>
 #include "fib_lookup.h"
 
+#ifdef CONFIG_VPNHIDE
+extern bool vpnhide_is_target_uid(void);
+extern bool vpnhide_is_vpn_ifname(const char *name);
+extern bool vpnhide_debug_enabled;
+#endif
+
 static int call_fib_entry_notifier(struct notifier_block *nb, struct net *net,
 				   enum fib_event_type event_type, u32 dst,
 				   int dst_len, struct fib_alias *fa)
@@ -2810,6 +2816,16 @@ static int fib_route_seq_show(struct seq_file *seq, void *v)
 
 		if (fi) {
 			struct fib_nh_common *nhc = fib_info_nhc(fi, 0);
+#ifdef CONFIG_VPNHIDE
+			if (vpnhide_is_target_uid() &&
+			    nhc->nhc_dev &&
+			    vpnhide_is_vpn_ifname(nhc->nhc_dev->name)) {
+				if(vpnhide_debug_enabled)
+					pr_info("vpnhide: fib_route_seq_show: hiding route for %s\n",
+					    nhc->nhc_dev->name);
+				continue;
+			}
+#endif			
 			__be32 gw = 0;
 
 			if (nhc->nhc_gw_family == AF_INET)
```
