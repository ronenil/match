/*******************************************************************************
  Match Action Table Configuration Agent
  Author: John Fastabend <john.r.fastabend@intel.com>
  Copyright (c) <2015>, Intel Corporation

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <getopt.h>

#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/socket.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/ctrl.h>

#include <linux/if_ether.h>

#include "if_match.h"
#include "matchlib.h"
#include "matchlib_nl.h"
#include "match_version.h"

#ifdef PRIx64
#undef PRIx64
#define PRIx64	"llx"
#endif /* PRIx64 */

#ifdef SCNx64
#undef SCNx64
#define SCNx64	"llx"
#endif /* SCNx64 */

#ifdef SCNu64
#undef SCNu64
#define SCNu64	"llu"
#endif /* SCNu64 */

static struct nl_sock *nsd;
static char *progname;

static void process_rx_message(int verbose);

static int
get_match_arg(int argc, char **argv, bool need_value, bool need_mask_type,
		struct net_mat_field_ref *match, const char **valid_keyword_list);

static int
get_action_arg(int argc, char **argv, bool need_args,
		struct net_mat_action *action);

static int
match_destroy_tbl_send(int verbose, uint32_t pid, int family,
		unsigned int ifindex, int argc, char **argv);

static int
match_create_tbl_send(int verbose, uint32_t pid, int family,
		unsigned int ifindex, int argc, char **argv, uint8_t cmd);

static int
rule_del_send(int verbose, uint32_t pid, int family, unsigned int ifindex,
		int argc, char **argv);

static int
rule_set_send(int verbose, uint32_t pid, int family, unsigned int ifindex,
		int argc, char **argv);

static int
rule_get_send(int verbose, uint32_t pid, int family, unsigned int ifindex,
		int argc, char **argv);

static int
match_send_recv(int verbose, uint32_t pid, int family, uint32_t ifindex,
		uint8_t cmd);

static bool is_valid_keyword(char **argv, const char **valid_keyword_list);

static void match_usage(void)
{
	printf("Usage: %s [OPTION...] COMMAND\n", progname);
	printf("\n");
	printf("Options:\n");
	printf("  -f FAMILY  netlink family\n");
	printf("  -g         display graphs in DOT format\n");
	printf("  -h         display this help message and exit\n");
	printf("  -p PID     pid of userspace match daemon\n");
	printf("  -s         silence verbose printing\n");
	printf("  --version  display Match interface version and exit\n");
	printf("\n");
	printf("Commands:\n");
	printf("  create            create a match action table\n");
	printf("  destroy           destroy a match action table\n");
	printf("  update            update a match action table attribute\n");
	printf("  del_rule          delete an existing rule from a table\n");
	printf("  set_rule          set a new rule in a match action table\n");
	printf("  get_rules         display rules in a table\n");
	printf("  get_actions       display actions in the pipeline\n");
	printf("  get_graph         display match action table graph\n");
	printf("  get_header_graph  display header graph\n");
	printf("  get_headers       display headers in the pipeline\n");
	printf("  get_tables        display match action tables\n");
	printf("  lport_lookup      display pci to logical port maps\n");
	printf("  phys_port_lookup  display logical port to physical port map\n");
	printf("  get_ports         display logical port info\n");
	printf("  set_port          set port attribute\n");
}

static void create_usage(void)
{
	printf("Usage: %s create source NUM name STRING [id NUM] size NUM [match MATCH...] [action ACTION [ACTION_ARG...]...]\n", progname);
	printf("\n");
	printf("Where:\n");
	printf("  source is the parent table id\n");
	printf("  name   is the name of the new table\n");
	printf("  id     is the id of the new table\n");
	printf("  size   is the maximum number of rules the new table can support\n");
	printf("\n");
	printf("Matches:\n");
	printf("  MATCH           : HEADER_INSTANCE.FIELD MASK_TYPE\n");
	printf("  HEADER_INSTANCE : string (e.g. ethernet)\n");
	printf("  FIELD           : string (e.g. dst_mac)\n");
	printf("  MASK_TYPE       : mask|exact\n");
	printf("For Example: match ethernet.dst_mac exact\n");
	printf("\n");
	printf("The specified matches must be a subset of the source table's matches.\n");
	printf("If no matches are specified, the new table will support the same matches as the source table.\n");
	printf("\n");
	printf("Actions:\n");
	printf("  ACTION : string (e.g. set_egress_port)\n");
	printf("\n");
	printf("The specified actions must be a subset of the source table's actions.\n");
	printf("If no actions are specified, the new table will support the same actions as the source table.\n");
}

static void destroy_usage(void)
{
	printf("Usage: %s destroy source NUM (name STRING | id NUM)\n", progname);
	printf("\n");
	printf("Where:\n");
	printf("  source is the parent table id\n");
	printf("  name   is the name of the table to destroy\n");
	printf("  id     is the id of the table to destroy\n");
}

static void update_usage(void)
{
	printf("Usage: %s update id NUM [attrib ATTRIB [ATTRIB_VALUE...]...]\n", progname);
	printf("\n");
	printf("Where:\n");
	printf("  id     is the id of an existing table\n");
	printf("\n");
	printf("attrib:\n");
	printf("  ATTRIB          : string (e.g. name of attribute)\n");
	printf("  ATTRIB_VALUE    : value of ATTRIB\n");
	printf("For Example: attrib vxlan_dst_mac 00:11:22:33:44:55\n");
	printf("\n");
}

static void del_rule_usage(void)
{
	printf("Usage: %s del_rule handle NUM table NUM\n", progname);
	printf("\n");
	printf("Where:\n");
	printf("  handle is the id of the rule to delete\n");
	printf("  table  is the table id from which to delete the rule\n");
}

static void set_rule_usage(void)
{
	printf("Usage: %s set_rule prio NUM handle NUM table NUM match MATCH [match MATCH...] action ACTION [ACTION_ARG...] [action ACTION [ACTION_ARG...]...]\n", progname);
	printf("\n");
	printf("Where:\n");
	printf("  prio   is the priority of new rule (1 is lowest)\n");
	printf("  handle is the id of new rule\n");
	printf("  table  is the table id in which to add the rule\n");
	printf("  match  is one or more criteria for the rule to match\n");
	printf("  action is one or more actions to apply\n");
	printf("\n");
	printf("Matches:\n");
	printf("  MATCH           : HEADER_INSTANCE.FIELD MATCH_VALUE MASK_VALUE\n");
	printf("  HEADER_INSTANCE : string (e.g. ethernet)\n");
	printf("  FIELD           : string (e.g. dst_mac)\n");
	printf("  MATCH_VALUE     : the value to match\n");
	printf("  MASK_VALUE      : the mask to use against the match value\n");
	printf("For example: match ethernet.dst_mac 00:01:02:03:04:05 ff:ff:ff:ff:ff:ff\n");
	printf("If MASK_VALUE is not specified then 'exact' mask is assumed\n");
	printf("\n");
	printf("Actions:\n");
	printf("  ACTION      : ACTION_NAME [ACTION_ARG...]\n");
	printf("  ACTION_NAME : string (e.g. set_egress_port)\n");
	printf("  ACTION_ARG  : action argument\n");
	printf("For example: action set_egress_port 5\n");
}

static void get_rules_usage(void)
{
	printf("Usage: %s get_rules table NUM [min NUM] [max NUM]\n", progname);
	printf("Where:\n");
	printf("  table  is the table id from which to get the rules\n");
	printf("  min    is the minimum rule id in a range of rule ids\n");
	printf("  max    is the maximum rule id in a range of rule ids\n");
}

static void get_lport_usage(void)
{
	printf("Usage: %s lookup_lport (pci BUS:DEVICE.FUNCTION | mac MACADDR)\n", progname);
	printf("Where:\n");
	printf("  pci    is the pci BUS:DEVICE.FUNCTION of the port to lookup\n");
	printf("  mac    is the MAC Address of the port to lookup\n");
	printf("Note: Exactly one pci/mac argument is required\n");
}

static void get_phys_port_usage(void)
{
	printf("Usage: %s phys_port_lookup lport <lport>\n", progname);
	printf("Where:\n");
	printf("  lport  is the logical port number to convert to physical\n");
}

static void get_port_usage(void)
{
	printf("Usage: %s get_ports [min NUM] [max NUM]\n", progname);
	printf("Where:\n");
	printf(" min	is the first port to print\n");
	printf(" max	is the last port to print\n");
}


static void set_port_usage(void)
{
	printf("Usage: %s set_port port NUM [speed NUM] [state NUM] [max_frame_size NUM] "
	       "[def_vlan NUM] [def_priority NUM] [drop_tagged VAL] [drop_untagged VAL] "
	       "[vlans VLAN[,VLAN...] [loopback VAL] [learning VAL] [update_dscp VAL] [update_ttl VAL]"
	       "[update_dmac VAL] [update_smac VAL] [update_vlan VAL] [mcast_flooding VAL]\n",
		progname);
	printf(" state: up down\n");
	printf(" speed: integer speed\n");
	printf(" max_frame_size: integer maximum frame size\n");
	printf(" def_vlan: integer port's default VLAN (1 - 4095)\n");
	printf(" def_priority: integer port's default VLAN priority (0 - 7)\n");
	printf(" drop_tagged: dropping tagged frames on ingress (enabled/disabled)\n");
	printf(" drop_untagged: dropping untagged frames on ingress (enabled/disabled)\n");
	printf(" vlans: set vlan membership set (0-4095)\n");
	printf(" loopback: tx2rx loopback on port (enabled/disabled)\n");
	printf(" learning: Learning of source addresses on this port (enabled/disabled)\n");
	printf(" update_dscp: Port may modify the DSCP on outgoing frames (enabled/disabled)\n");
	printf(" update_ttl: Port should decrement the TTL field on outgoing routed frames (enabled/disabled)\n");
	printf(" update_dmac: Port updates Destination MAC on a routed frames (enabled/disabled)\n");
	printf(" update_smac: Port updates Source MAC on a routed frames (enabled/disabled)\n");
	printf(" update_vlan: Port updates VLAN on a routed frames (enabled/disabled)\n");
	printf(" mcast_flooding: Multicast frames forwarding (enabled/disabled)\n");
}

static struct nla_policy match_get_tables_policy[NET_MAT_MAX+1] = {
	[NET_MAT_IDENTIFIER_TYPE] = { .type = NLA_U32 },
	[NET_MAT_IDENTIFIER]	= { .type = NLA_U32 },
	[NET_MAT_TABLES]	= { .type = NLA_NESTED },
	[NET_MAT_HEADERS]	= { .type = NLA_NESTED },
	[NET_MAT_ACTIONS]	= { .type = NLA_NESTED },
	[NET_MAT_HEADER_GRAPH]	= { .type = NLA_NESTED },
	[NET_MAT_TABLE_GRAPH]	= { .type = NLA_NESTED },
	[NET_MAT_RULES]	= { .type = NLA_NESTED },
	[NET_MAT_PORTS]	= { .type = NLA_NESTED },
};

/*
 * Parse a MAC address
 *
 * @param dst
 *   Location to store the parsed address
 * @param src
 *   String containing the address to parse. The address can be
 *   colon seperated, a hex value, or a decimal value.
 * @return
 *   0 on success or -EINVAL if there is a parsing error.
 */
static int
match_macaddr2u64(__u64 *dst, const char *src)
{
	int err;

	if (!src || !dst || ETH_ALEN != 6)
		return -EINVAL;

	if (strchr(src, ':')) {
		__u8 tmp[ETH_ALEN];

		err = sscanf(src,
			"%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8,
			&tmp[5], &tmp[4], &tmp[3], &tmp[2], &tmp[1], &tmp[0]);

		if (err != ETH_ALEN)
			return -EINVAL;
		else
			memcpy(dst, tmp, ETH_ALEN);
	} else {
		err = sscanf(src, "0x%" SCNx64 "", dst);
		if (err != 1) {
			err = sscanf(src, "%" SCNu64 "", dst);
			if (err != 1)
				return -EINVAL;
		}

		if (*dst > 0xffffffffffff)
			return -EINVAL;
	}

	return 0;
}

static void match_cmd_get_tables(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	struct net_mat_tbl *tables = NULL;
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get tables msg\n");
		return;
	}

	if (match_nl_table_cmd_to_type(NULL, NET_MAT_TABLES, tb))
		return;

	if (tb[NET_MAT_TABLES]) {
		match_get_tables(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_TABLES], &tables);
		match_push_tables_a(tables);
	}
}

static void match_cmd_get_headers(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get headers msg\n");
		return;
	}

	if (match_nl_table_cmd_to_type(NULL, NET_MAT_HEADERS, tb))
		return;

	if (tb[NET_MAT_HEADERS])
		match_get_headers(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_HEADERS], NULL);
}

static void match_cmd_get_actions(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get actions msg\n");
		return;
	}

	if (match_nl_table_cmd_to_type(NULL, NET_MAT_ACTIONS, tb))
		return;

	if (tb[NET_MAT_ACTIONS]) {
		struct net_mat_action *a;

		match_get_actions(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_ACTIONS], &a);
		match_push_actions_ary(a);
	}
}

static void match_cmd_get_headers_graph(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get header graph msg\n");
		return;
	}

	if (match_nl_table_cmd_to_type(NULL, NET_MAT_HEADER_GRAPH, tb))
		return;

	if (tb[NET_MAT_HEADER_GRAPH])
		match_get_hdrs_graph(verbose ? mat_stream_stdout() : NULL, verbose,
				tb[NET_MAT_HEADER_GRAPH], NULL);
}

static void match_cmd_get_table_graph(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get table graph msg\n");
		return;
	}

	if (match_nl_table_cmd_to_type(NULL, NET_MAT_TABLE_GRAPH, tb))
		return;

	if (tb[NET_MAT_TABLE_GRAPH])
		match_get_tbl_graph(verbose ? mat_stream_stdout() : NULL, verbose,
			tb[NET_MAT_TABLE_GRAPH], NULL);
}

static void match_cmd_get_rules(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get rules msg\n");
		return;
	}

	err = match_nl_table_cmd_to_type(NULL, 0, tb);
	if (err == -ENOMSG) {
		fprintf(stdout, "Table empty\n");
		return;
	} else if (err) {
		fprintf(stderr, "Warning: recevied cmd without valid attribute expected %i\n", NET_MAT_RULES);
		return;
	}

	if (tb[NET_MAT_RULES])
		match_get_rules(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_RULES], NULL);
}

static void match_cmd_set_rules(struct match_msg *msg, int verbose)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse set rules msg\n");
		return;
	}

	err = match_nl_table_cmd_to_type(NULL, 0, tb);
	if (err)
		return;

	if (tb[NET_MAT_RULES]) {
		fprintf(stderr, "Failed to set:\n");
		match_get_rules(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_RULES], NULL);
	}
}

static void match_cmd_del_rules(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse del rules msg\n");
		return;
	}

	fprintf(stderr, "delete rule cmd not supported\n");
}

static void
match_cmd_update_rules(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse update tables msg\n");
		return;
	}
	fprintf(stderr, "update match cmd not supported\n");
}

static void
match_cmd_create_table(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse create table msg\n");
		return;
	}
}

static void
match_cmd_destroy_table(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse destroy table msg\n");
		return;
	}
}

static void
match_cmd_get_lport(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	struct net_mat_port *port = NULL;
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get ports msg\n");
		return;
	}

	err = match_nl_table_cmd_to_type(NULL, NET_MAT_PORTS, tb);
	if (err)
		return;

	if (tb[NET_MAT_PORTS]) {
		err = match_get_ports(NULL, tb[NET_MAT_PORTS], &port);
		if (err)
			fprintf(stderr, "Warning: unable to parse get ports\n");
		else
			fprintf(stdout, "Logical Port: %d\n", port->port_id);

		free(port);
	}
}

static void
match_cmd_get_ports(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse get ports msg\n");
		return;
	}

	err = match_nl_table_cmd_to_type(NULL, NET_MAT_PORTS, tb);
	if (err)
		return;

	if (tb[NET_MAT_PORTS]) {
		err = match_get_ports(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_PORTS], NULL);
		if (err)
			fprintf(stderr, "Warning: unable to parse get ports\n");
	}
}

static void
match_cmd_set_ports(struct match_msg *msg, int verbose __unused)
{
	struct nlmsghdr *nlh = msg->msg;
	struct nlattr *tb[NET_MAT_MAX+1];
	int err;

	err = genlmsg_parse(nlh, 0, tb, NET_MAT_MAX, match_get_tables_policy);
	if (err < 0) {
		fprintf(stderr, "Warning: unable to parse set ports msg\n");
		return;
	}

	err = match_nl_table_cmd_to_type(NULL, 0, tb);
	if (err)
		return;

	if (tb[NET_MAT_PORTS]) {
		err = match_get_ports(verbose ? mat_stream_stdout() : NULL, tb[NET_MAT_PORTS], NULL);
		if (err)
			fprintf(stderr, "Warning: unable to parse get ports\n");
	}
}

static void(*type_cb[NET_MAT_CMD_MAX+1])(struct match_msg *, int verbose) = {
	[NET_MAT_TABLE_CMD_GET_TABLES]        = match_cmd_get_tables,
	[NET_MAT_TABLE_CMD_GET_HEADERS]       = match_cmd_get_headers,
	[NET_MAT_TABLE_CMD_GET_ACTIONS]       = match_cmd_get_actions,
	[NET_MAT_TABLE_CMD_GET_HDR_GRAPH]     = match_cmd_get_headers_graph,
	[NET_MAT_TABLE_CMD_GET_TABLE_GRAPH]   = match_cmd_get_table_graph,
	[NET_MAT_TABLE_CMD_GET_RULES]         = match_cmd_get_rules,
	[NET_MAT_TABLE_CMD_SET_RULES]         = match_cmd_set_rules,
	[NET_MAT_TABLE_CMD_DEL_RULES]         = match_cmd_del_rules,
	[NET_MAT_TABLE_CMD_UPDATE_RULES]      = match_cmd_update_rules,
	[NET_MAT_TABLE_CMD_CREATE_TABLE]      = match_cmd_create_table,
	[NET_MAT_TABLE_CMD_DESTROY_TABLE]     = match_cmd_destroy_table,
	[NET_MAT_TABLE_CMD_UPDATE_TABLE]      = match_cmd_create_table,
	[NET_MAT_PORT_CMD_GET_PORTS]          = match_cmd_get_ports,
	[NET_MAT_PORT_CMD_GET_LPORT]          = match_cmd_get_lport,
	[NET_MAT_PORT_CMD_GET_PHYS_PORT]      = match_cmd_get_ports,
	[NET_MAT_PORT_CMD_SET_PORTS]          = match_cmd_set_ports,
};

void process_rx_message(int verbose)
{
	struct match_msg *msg;
	int err;
	int type;
	sigset_t bs;

	sigemptyset(&bs);
	sigaddset(&bs, SIGINT);

	/*
	 * Continue processing messages until an NLMSG_DONE message is
	 * received, a message without the NLM_F_MULTI flag is received,
	 * or an error occurs.
	 */
	for (;;) {
		sigprocmask(SIG_UNBLOCK, &bs, NULL);
		msg = match_nl_recv_msg(nsd, &err);
		sigprocmask(SIG_BLOCK, &bs, NULL);

		if (msg) {
			struct nlmsghdr *nlh = msg->msg;
			struct genlmsghdr *glh = nlmsg_data(nlh);

			if (nlh->nlmsg_type == NLMSG_DONE) {
				match_nl_free_msg(msg);
				break;
			}

			type = glh->cmd;
			type_cb[type](msg, verbose);

			if (!(nlh->nlmsg_flags & NLM_F_MULTI)) {
				match_nl_free_msg(msg);
				break;
			}
			match_nl_free_msg(msg);
		} else {
			break;
		}
	}
}

#define next_arg() do { argv++; argc--; } while (0)

 /**
 * Determine if argv matches any string in a list of keywords
 *
 * @param argv
 * 	Value to search for
 * @param valid_keyword_list
 * 	A NULL terminated array of strings
 * @return true if argv is found in list, false otherwise
 */

bool is_valid_keyword(char **argv, const char **valid_keyword_list)
{
	int  i;
	for (i = 0; valid_keyword_list[i]; i++) {
		if (strcmp(*argv,  valid_keyword_list[i]) == 0)
			return true;
	}
	return false;
}


static int match_set_value_ll(struct net_mat_field_ref *match,
			      unsigned long long value)
{
	if (!match)
		return -EINVAL;

	switch (match->type) {
	case NET_MAT_FIELD_REF_ATTR_TYPE_U8:
		match->v.u8.mask_u8 = (__u8)value;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U16:
		match->v.u16.mask_u16 = (__u16)value;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U32:
		match->v.u32.mask_u32 = (__u32)value;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U64:
		match->v.u64.mask_u64 = (__u64)value;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_IN6:
	default:
		fprintf(stderr,"Error: Invalid match attr type (%d)\n",
			match->type);
		return -EINVAL;
	}

	return 0;
}


static int match_set_mask_ll(struct net_mat_field_ref *match,
			     unsigned long long mask)
{
	if (!match)
		return -EINVAL;

	switch (match->type) {
	case NET_MAT_FIELD_REF_ATTR_TYPE_U8:
		match->v.u8.mask_u8 = (__u8)mask;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U16:
		match->v.u16.mask_u16 = (__u16)mask;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U32:
		match->v.u32.mask_u32 = (__u32)mask;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_U64:
		match->v.u64.mask_u64 = (__u64)mask;
		break;
	case NET_MAT_FIELD_REF_ATTR_TYPE_IN6:
	default:
		fprintf(stderr,"Error: Invalid match attr type (%d)\n",
			match->type);
		return -EINVAL;
	}

	return 0;
}


static int match_set_exact_mask(struct net_mat_field_ref *match)
{
	unsigned long long mask = ULLONG_MAX;


	if (!match)
		return -EINVAL;

	if (match->type == NET_MAT_FIELD_REF_ATTR_TYPE_IN6) {
		match->v.in6.mask_in6.s6_addr32[0] = (__u32)-1;
		match->v.in6.mask_in6.s6_addr32[1] = (__u32)-1;
		match->v.in6.mask_in6.s6_addr32[2] = (__u32)-1;
		match->v.in6.mask_in6.s6_addr32[3] = (__u32)-1;

		return 0;
	}

	return match_set_mask_ll(match, mask);
}

static __u32 field_get_attr_type(const struct net_mat_field *field)
{
	if (!field)
		return NET_MAT_FIELD_REF_ATTR_TYPE_UNSPEC;

	if (field->bitwidth <= 8)
		return NET_MAT_FIELD_REF_ATTR_TYPE_U8;
	else if (field->bitwidth <= 16)
		return NET_MAT_FIELD_REF_ATTR_TYPE_U16;
	else if (field->bitwidth <= 32)
		return NET_MAT_FIELD_REF_ATTR_TYPE_U32;
	else if (field->bitwidth <= 64)
		return NET_MAT_FIELD_REF_ATTR_TYPE_U64;
	else if (field->bitwidth == 128)
		return NET_MAT_FIELD_REF_ATTR_TYPE_IN6;

	/* bitwidth greater than 64 except for 128 */
	return NET_MAT_FIELD_REF_ATTR_TYPE_UNSPEC;
}


static unsigned long long field_max_value(const struct net_mat_field *field)
{
	unsigned long long value_max;


	if (!field)
		return 0;

	if (field->bitwidth > 64)
		return 0;

	value_max = ((1ULL << field->bitwidth) - 1);

	return value_max;
}


static int parse_ull(const char *str, unsigned long long *val_p)
{
	int base = 0;
	char *end_p = NULL;

	if (!str || !val_p)
		return -EINVAL;

	errno = 0;
        *val_p = strtoul(str, &end_p, base);

	if (errno)
		return -errno;

	/* Parsing successful if entire string has been consumed */
	if ((*str != '\0') && (end_p && (*end_p == '\0')))
		return 0;

	return -EINVAL;
}


static int field_parse_value(const struct net_mat_field *field,
			     const char *str,
			     unsigned long long *value_p)
{
	int err;
	unsigned long long value;
	unsigned long long value_max;;


	if (!field || !str || !value_p)
		return -EINVAL;

	err = parse_ull(str, &value);

	value_max = field_max_value(field);

	if ((err == -ERANGE)
	    || (!err && (! (value <= value_max)))) {
		fprintf(stderr, "'%s' is out-of-range for %s, must be at most %llu(0x%llx)\n",
			str, field->name, value_max, value_max);
		return -ERANGE;
	}

	if (!err) {
		*value_p = value;
		return 0;
	}

	return err;
}


static int match_parse_value_as_number(struct net_mat_field_ref *match,
				       const struct net_mat_field *field,
				       const char *str)
{
	int err;
	unsigned long long value;


	err = field_parse_value(field, str, &value);
	if (err)
		return err;

	err = match_set_value_ll(match, value);

	return err;
}

static int match_parse_mask_as_number(struct net_mat_field_ref *match,
				      const struct net_mat_field *field,
				      const char *str)
{
	int err;
	unsigned long long mask;


	err = field_parse_value(field, str, &mask);
	if (err)
		return err;

	err = match_set_mask_ll(match, mask);

	return err;
}


static int match_parse_value_specific(struct net_mat_field_ref *match,
				      const struct net_mat_field *field,
				      const char *str)
{
	if (!match || !str || !field)
		return -EINVAL;

	switch (match->type) {
	case NET_MAT_FIELD_REF_ATTR_TYPE_U8:
		fprintf(stderr, "'%s' is invalid u8 value for %s\n",
			str, field->name);
		return -EINVAL;
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U16:
		fprintf(stderr, "'%s' is invalid u16 value for %s\n",
			str, field->name);
		return -EINVAL;
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U32:
		if (!inet_aton(str,
			       (struct in_addr *)&match->v.u32.value_u32)) {
			if (strchr(str, '.')) {
				fprintf(stderr, "'%s' is invalid IP address value for %s\n",
					str, field->name);
			} else {
				fprintf(stderr, "'%s' is invalid u32 value for %s\n",
					str, field->name);
			}
			return -EINVAL;
		}
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U64:
		if (match_macaddr2u64(&match->v.u64.value_u64, str)) {
			if (strchr(str, ':')) {
				fprintf(stderr, "'%s' is invalid MAC address value for %s\n",
					str, field->name);
			} else {
				fprintf(stderr, "'%s' is invalid u64 value for %s\n",
					str, field->name);
			}
			return -EINVAL;
		}
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_IN6:
		if (!inet_pton(AF_INET6, str, &match->v.in6.value_in6)) {
			fprintf(stderr, "'%s' is invalid IPv6 address value for %s\n",
				str, field->name);
			return -EINVAL;
		}
		break;

	default:
		fprintf(stderr, "Invalid match attr type %d for %s\n",
			match->type, field->name);
		return -EINVAL;
	}

	return 0;
}


static int match_parse_mask_specific(struct net_mat_field_ref *match,
				      const struct net_mat_field *field,
				      const char *str)
{
	if (!match || !str || !field)
		return -EINVAL;

	switch (match->type) {
	case NET_MAT_FIELD_REF_ATTR_TYPE_U8:
		fprintf(stderr, "'%s' is invalid u8 mask for %s\n",
			str, field->name);
		return -EINVAL;
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U16:
		fprintf(stderr, "'%s' is invalid u16 mask for %s\n",
			str, field->name);
		return -EINVAL;
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U32:
		if (!inet_aton(str,
			       (struct in_addr *)&match->v.u32.mask_u32)) {
			if (strchr(str, '.')) {
				fprintf(stderr, "'%s' is invalid IP address mask for %s\n",
					str, field->name);
			} else {
				fprintf(stderr, "'%s' is invalid u32 mask for %s\n",
					str, field->name);
			}
			return -EINVAL;
		}
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_U64:
		if (match_macaddr2u64(&match->v.u64.mask_u64, str)) {
			if (strchr(str, ':')) {
				fprintf(stderr, "'%s' is invalid MAC address mask for %s\n",
					str, field->name);
			} else {
				fprintf(stderr, "'%s' is invalid u64 mask for %s\n",
					str, field->name);
			}
			return -EINVAL;
		}
		break;

	case NET_MAT_FIELD_REF_ATTR_TYPE_IN6:
		if (!inet_pton(AF_INET6, str, &match->v.in6.mask_in6)) {
			fprintf(stderr, "'%s' is invalid IPv6 address mask for %s\n",
				str, field->name);
			return -EINVAL;
		}
		break;

	default:
		fprintf(stderr, "Invalid match attr type %d for %s\n",
			match->type, field->name);
		return -EINVAL;
	}

	return 0;
}





static int get_match_arg(int argc, char **argv, bool need_value, bool need_mask_type,
			 struct net_mat_field_ref *match, const char **valid_keyword_list)
{
	char *strings, *instance, *s_fld;
	struct net_mat_hdr_node *hdr_node;
	struct net_mat_field *field;
	int advance = 0;
	int err = 0;


	next_arg();
	strings = *argv;
	advance++;

	if (*argv == NULL) {
		fprintf(stderr, "Error: missing match instance\n");
		return -EINVAL;
	}

	/* We use the instance name followed by the field in that instance
	 * to setup a rule. The instance name must be used to avoid
	 * ambiguity when a packet parser can support multiple stacked
	 * headers or tunnels and other such packets with multiples of the
	 * same header. This means here we have to unwind the string name
	 * from the user into a correct header_node and field nodes.
	 */
	instance = strtok(strings, ".");
	if (!instance) {
		fprintf(stderr, "invalid match instance input should be, \"instance.field\"\n");
		return -EINVAL;
	}

	s_fld = strtok(NULL, ".");
	if (!s_fld) {
		fprintf(stderr, "invalid match field input should be, \"instance.field\"\n");
		return -EINVAL;
	}

	match->instance = find_header_node(instance);
	if (!match->instance) {
		fprintf(stderr, "unknown instance `%s`, check \"get_header_graph\".\n",
			instance);
		return -EINVAL;
	}

	hdr_node = get_graph_node(match->instance);
	if (!hdr_node) { /* with an abundance of caution */
		fprintf(stderr, "graph_node lookup failed. Mostly likely a model bug\n");
		return -EINVAL;
	}

	/* For now only support parsing single header per node. Its not
	 * very clera what it means to support multiple header types or
	 * how we would infere the correct one. It might be best to
	 * codify the single header _only_ case. Also we require that
	 * hdrs be a valid pointer. Being overly cautious we check for
	 * it though.
	 */

	if (!hdr_node->hdrs) {
		fprintf(stderr, "%s(%i) node appears to be empty? Possible model bug\n",
			hdr_node->name, hdr_node->uid);
		return -EINVAL;
	}

	match->header = hdr_node->hdrs[0];
	match->field = find_field(s_fld, match->header);
	if (!match->field) {
		fprintf(stderr, "unknown field %s, check \"get_headers\".\n",
			s_fld);
		return -EINVAL;
	}

	field = get_fields(match->header, match->field);
	if (!field)
		return -EINVAL;

	if (need_mask_type) {
		advance++;
		next_arg();

		if (*argv == NULL) {
			/* *argv can be NULL if no mask is provided */
			fprintf(stderr, "match mask is required\n");
			return -EINVAL;
		}

		if (strcmp(*argv, "lpm") == 0) {
			match->mask_type = NET_MAT_MASK_TYPE_LPM;
		} else if (strcmp(*argv, "exact") == 0) {
			match->mask_type = NET_MAT_MASK_TYPE_EXACT;
		} else if (strcmp(*argv, "mask") == 0) {
			match->mask_type = NET_MAT_MASK_TYPE_MASK;
		} else {
			fprintf(stderr, "unknown mask type %s.\n", *argv);
			return -EINVAL;
		}
	}

	if (!need_value)
		return advance;

	next_arg();

	if (*argv == NULL) {
		fprintf(stderr, "Error: missing match value\n");
		return -EINVAL;
	}

	match->type = field_get_attr_type(field);
	/*
	 * First we try to parse value as a number
	 */
	err = match_parse_value_as_number(match, field, *argv);
	if (err == -ERANGE) {
		/*
		 * Out-of-range error already logged during parsing
		 */
		return -ERANGE;
	} else if (err) {
		/*
		 * Next we try specific value types (IP, MAC, IPv6) as implied by attr type
		 */
		err = match_parse_value_specific(match, field, *argv);
		if (err) {
			/*
			 * Invalid format already logged during parsing
			 */
			return -EINVAL;
		}
	}
	advance++;

	next_arg(); /* need a mask if its not an exact match */

	/* If end of the line is reached, or if the next value appearing on
	 * the command line appears in the list of keywords, then a mask
	 * value is not expected. Instead, 'exact' mask is assumed.
	 */
	if (*argv == NULL || is_valid_keyword(argv, valid_keyword_list)) {
		if (match_set_exact_mask(match)) {
			fprintf(stderr,"Error: Invalid mask type\n");
			return -EINVAL;
		}
		return advance;
	}

	/*
	 * First we try to parse mask as a number
	 */
	err = match_parse_mask_as_number(match, field, *argv);
	if (err == -ERANGE) {
		/*
		 * Out-of-range error already logged during parsing
		 */
		return -ERANGE;
	} else if (err) {
		/*
		 * Next we try specific mask types (IP, MAC, IPv6) as implied by attr type
		 */
		err = match_parse_mask_specific(match, field, *argv);
		if (err) {
			/*
			 * Invalid format already logged during parsing
			 */
			return -EINVAL;
		}
	}
	advance++;

	return advance;
}


int get_action_arg(int argc, char **argv, bool need_args,
		   struct net_mat_action *action)
{
	struct net_mat_action *a;
	unsigned int i, num_args = 0, reqs_args = 0;
	int err = 0, advance = 0;
	__u32 type = 0;
	char *has_dots, *name = NULL;
	bool variadic = false;

	next_arg();
	advance++;

	if (*argv == NULL) {
		fprintf(stderr, "Error: missing action name\n");
		return -EINVAL;
	}

	i = find_action(*argv);
	if (!i) {
		fprintf(stderr, "Warning: unknown action\n");
		return -EINVAL;
	}

	a = get_actions(i);
	if (!a) {
		fprintf(stderr, "Error: missing actions\n");
		return -EINVAL;
	}
	for (i = 0; a->args && a->args[i].type != NET_MAT_ACTION_ARG_TYPE_UNSPEC; i++)
		reqs_args++;

	action->uid = a->uid;
	if (a->name)
		action->name = strdup(a->name);

	if (!reqs_args || !need_args)
		goto done;

	next_arg();

	/* If the type of argument is variadic then we need to consume all
	 * remaining arguments on the command line. This is possible because
	 * variadic actions are always specified last.
	 * Also note we need to be careful to reset argv pointer after we
	 * count the arguments so that we can then parse them below, see
	 * old, argv handling in the if block here.
	 */
	if (a->args[reqs_args-1].type == NET_MAT_ACTION_ARG_TYPE_VARIADIC) {
		char **old = argv;

		variadic = true;

		while (*argv && strcmp(*argv, "action") != 0) {
			num_args++;
			next_arg();
		}
		argv = old;
	} else {
		num_args = reqs_args;
	}

	action->args = calloc(num_args + 1,
			sizeof(struct net_mat_action_arg));
	if (!action->args) {
		fprintf(stderr, "Error: action args calloc failure\n");
		free(action->name);
		return -1;
	}

	for (i = 0; i < num_args; i++) {
		if (i < reqs_args &&
		    a->args[i].type != NET_MAT_ACTION_ARG_TYPE_VARIADIC) {
			type = a->args[i].type;
			name = a->args[i].name;
		}
		if (*argv == NULL) {
			if(variadic) {
				return ++advance;
			} else {
				fprintf(stderr, "Error: missing action arg. expected `%s %s`\n",
					net_mat_action_arg_type_str(action->args[i].type),
					action->args[i].name);
				return -EINVAL;
			}
		}

		action->args[i].type = type;
		if (name)
			action->args[i].name = strdup(name);

		switch (type) {
		case NET_MAT_ACTION_ARG_TYPE_U8:
			err = sscanf(*argv, "0x%" SCNx8 "",
					&action->args[i].v.value_u8);
			if (err != 1)
				err = sscanf(*argv, "%" SCNu8 "",
						&action->args[i].v.value_u8);
			break;
		case NET_MAT_ACTION_ARG_TYPE_U16:
			err = sscanf(*argv, "0x%" SCNx16 "",
					&action->args[i].v.value_u16);
			if (err != 1)
				err = sscanf(*argv, "%" SCNu16 "",
					&action->args[i].v.value_u16);
			break;
		case NET_MAT_ACTION_ARG_TYPE_U32:
			has_dots = strtok(*argv, " ");
			if (!has_dots) {
				fprintf(stderr, "Invalid u32 bit value %s\n", *argv);
				return -EINVAL;
			}
			if (strchr(has_dots, '.')) {
				err = inet_aton(*argv,
				(struct in_addr *)&action->args[i].v.value_u32);
				if (!err)
					return -EINVAL;
			} else {
				err = sscanf(*argv, "0x%" SCNx32 "",
						&action->args[i].v.value_u32);
				if (err != 1)
					err = sscanf(*argv, "%" SCNu32 "",
						&action->args[i].v.value_u32);
				if (err != 1)
					return -EINVAL;
			}
			if (err != 1)
				err = sscanf(*argv, "%" PRIu32 "",
					&action->args[i].v.value_u32);
			break;
		case NET_MAT_ACTION_ARG_TYPE_U64:
			errno = 0;

			if (match_macaddr2u64(&action->args[i].v.value_u64,
			                     *argv)) {
				fprintf(stderr,
				        "Error: Invalid u64 or MAC address value (%s)\n",
				        *argv);
				return -EINVAL;
			} else {
				/* one field was parsed */
				err = 1;
			}

			break;
		case NET_MAT_ACTION_ARG_TYPE_IN6:
			errno = 0;
			if (!inet_pton(AF_INET6, *argv, &action->args[i].v.value_in6)) {
				fprintf(stderr,
				        "Error: Invalid IPv6 address (%s)\n",
				        *argv);
				return -EINVAL;
			} else {
				/* one field was parsed */
				err = 1;
			}
			break;
		case NET_MAT_ACTION_ARG_TYPE_NULL:
		case NET_MAT_ACTION_ARG_TYPE_VARIADIC:
			break;
		case NET_MAT_ACTION_ARG_TYPE_UNSPEC:
		case __NET_MAT_ACTION_ARG_TYPE_VAL_MAX:
			exit(-1);
		}

		if (err != 1)
			return -EINVAL;

		next_arg();
		advance++;
	}

done:
	return advance;
}

#define MAX_MATCHES 50
#define MAX_ACTIONS 50
#define MAX_ATTRIBS 50

#define NET_MAT_MAXNAME 120

int
match_destroy_tbl_send(int verbose, uint32_t pid, int family,
		unsigned int ifindex, int argc, char **argv)
{
	uint8_t cmd = NET_MAT_TABLE_CMD_DESTROY_TABLE;
	struct net_mat_tbl table;
	struct nlattr *nest, *nest1;
	struct match_msg *msg;
	int err = 0;

	memset(&table, 0, sizeof(table));

	while (argc > 0) {
		if (strcmp(*argv, "name") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing name\n");
				return -EINVAL;
			}

			table.name = strndup(*argv, NET_MAT_MAXNAME);
			if (!table.name) {
				fprintf(stderr, "Error: name too long\n");
				destroy_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "source") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing source\n");
				return -EINVAL;
			}

			/* todo: fix ugly type cast */
			err = sscanf(*argv, "%u", &table.source);
			if (err < 0) {
				fprintf(stderr, "Error: source invalid\n");
				destroy_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "id") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing id\n");
				return -EINVAL;
			}

			/* todo: fix ugly type cast */
			err = sscanf(*argv, "%u", &table.uid);
			if (err < 0) {
				fprintf(stderr, "Error: id invalid\n");
				destroy_usage();
				return -EINVAL;
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			destroy_usage();
			exit(-1);
		}
		argc--; argv++;
	}

	if (err < 0) {
		printf("Invalid argument\n");
		destroy_usage();
		exit(-1);
	}

	if (!(table.name || table.uid)) {
		/* neither name or id is provided */
		fprintf(stderr, "Error: name or id is required\n");
		destroy_usage();
		return -EINVAL;
	} else if (!table.uid) {
		/* name provided, but not id */
		table.uid = get_table_id(table.name);
		if (!table.uid) {
			fprintf(stderr, "Error: name invalid\n");
			destroy_usage();
			return -EINVAL;
		}
	} else if (!table.name) {
		/* id provided, but not name */
		char *name = table_names(table.uid);
		if (!name) {
			fprintf(stderr, "Error: id invalid\n");
			destroy_usage();
			return -EINVAL;
		}
		table.name = strndup(name, NET_MAT_MAXNAME);
		if (!table.name) {
			fprintf(stderr, "Error: name too long\n");
			destroy_usage();
			return -EINVAL;
		}
	} else {
		/* both name and id provided, make sure they refer to
		 * the same table */
		if (get_table_id(table.name) != table.uid) {
			fprintf(stderr, "Error: name/id mismatch\n");
			destroy_usage();
			return -EINVAL;
		}
	}

	if (!table.source) {
		fprintf(stderr, "Error: source is required\n");
		destroy_usage();
		return -EINVAL;
	}

	pp_table(mat_stream_stdout(), &table);

	/* open generic netlink socket with match table api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMSG;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	nest = nla_nest_start(msg->nlbuf, NET_MAT_TABLES);
	if (!nest) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}
	nest1 = nla_nest_start(msg->nlbuf, NET_MAT_TABLE);
	match_put_table(msg->nlbuf, &table);
	nla_nest_end(msg->nlbuf, nest1);
	nla_nest_end(msg->nlbuf, nest);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);

	match_nl_free_msg(msg);
	return err;

}

static struct net_mat_named_value *
get_table_attribs(__u32 table_id)
{
	struct net_mat_tbl *t = get_tables(table_id);

	if (!t)
		return NULL;

	return t->attribs;
}
static int
get_attrib_arg(int argc, char **argv, __u32 table_uid, struct net_mat_named_value *value)
{
	struct net_mat_named_value *ref;
	int advance = 0, err = 0, i;
	char *name, *has_dots;

	next_arg();
	name = *argv;
	advance++;

	if (*argv == NULL) {
		fprintf(stderr, "Error: missing attribute name\n");
		return -EINVAL;
	}

	ref = get_table_attribs(table_uid);
	if (!ref) {
		fprintf(stderr, "Error: table (%i) does not support attributes\n",
			table_uid);
		return -EINVAL;
	}

	for (i = 0; ref[i].uid; i++) {
		if (strcmp(ref[i].name, name) == 0)
			break;
	}

	if (!ref[i].uid) {
		fprintf(stderr, "Error: table (%i) does not support attribute %s\n",
			table_uid, name);
		return -EINVAL;
	}

	value->uid = ref[i].uid;
	value->name = ref[i].name;

	if (!ref[i].write) {
		fprintf(stderr, "Error: table (%i) does not support setting %s\n",
			table_uid, name);
		return -EINVAL;
	}

	next_arg();
	if (*argv == NULL) {
		fprintf(stderr, "Error: missing attribute value\n");
		return -EINVAL;
	}

	switch (ref[i].type) {
	case NET_MAT_NAMED_VALUE_TYPE_U8:
		value->type = NET_MAT_NAMED_VALUE_TYPE_U8;
		err = sscanf(*argv, "0x%" SCNx8 "", &value->value.u8);
		if (err != 1)
			err = sscanf(*argv, "%" SCNu8 "", &value->value.u8);

		if (err != 1) {
			fprintf(stderr, "Invalid value %s, value must be 0xXX or integer\n",
				*argv);
			return -EINVAL;
		}
		break;
	case NET_MAT_NAMED_VALUE_TYPE_U16:
		value->type = NET_MAT_NAMED_VALUE_TYPE_U16;
		err = sscanf(*argv, "0x%" SCNx16 "", &value->value.u16);
		if (err != 1)
			err = sscanf(*argv, "%" SCNu16 "", &value->value.u16);

		if (err != 1) {
			fprintf(stderr, "Invalid value %s, value must be 0xXXXX or integer\n",
				*argv);
			return -EINVAL;
		}
		break;
	case NET_MAT_NAMED_VALUE_TYPE_U32:
		value->type = NET_MAT_NAMED_VALUE_TYPE_U32;
		has_dots = strtok(*argv, " ");
		if (!has_dots) {
			fprintf(stderr, "Invalid u32 bit value\n");
			return -EINVAL;
		}
		if (strchr(has_dots, '.')) {
			err = inet_aton(*argv,
				(struct in_addr *)&value->value.u32);
			if (!err) {
				fprintf(stderr, "Invalid value %s, looks like an IP address but is invalid.\n",
					*argv);
				return -EINVAL;
			}
		} else {
			err = sscanf(*argv, "0x%" SCNx32 "", &value->value.u32);
			if (err != 1)
				err = sscanf(*argv, "%" SCNu32 "",
					     &value->value.u32);
			if (err != 1) {
				fprintf(stderr, "Invalid u32 bit value %s\n",
					*argv);
				return -EINVAL;
			}
		}
		break;
	case NET_MAT_NAMED_VALUE_TYPE_U64:
		value->type = NET_MAT_NAMED_VALUE_TYPE_U64;
		errno = 0;

		if (match_macaddr2u64(&value->value.u64, *argv)) {
			fprintf(stderr,
				"Error: Invalid u64 or MAC address value (%s)\n",
				*argv);
			return -EINVAL;
		} else {
			/* one field was parsed */
			err = 1;
		}

		break;
	default:
		return -EINVAL;
	}
	advance++;
	return advance;
}

static void match_free_action(struct net_mat_action *a)
{
	int i;

	free(a->name);
	for (i = 0; a->args && a->args[i].type; i++)
		free(a->args[i].name);
}

int
match_create_tbl_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
		     int argc, char **argv, uint8_t cmd)
{
	struct nlattr *nest, *nest1;
	struct net_mat_named_value attribs[MAX_ATTRIBS];
	struct net_mat_field_ref matches[MAX_MATCHES];
	__u32 acts[MAX_ACTIONS];
	int match_count = 0, action_count = 0, attrib_count = 0;
	struct match_msg *msg;
	int err = 0, advance = 0;
	struct net_mat_tbl table;

	memset(&table, 0, sizeof(table));
	memset(matches, 0, sizeof(matches));
	memset(acts, 0, sizeof(acts));
	memset(attribs, 0, sizeof(attribs));
	table.matches = &matches[0];
	table.actions = &acts[0];
	table.attribs = &attribs[0];

	opterr = 0;
	while (argc > 0) {
		if (strcmp(*argv, "match") == 0) {
			if (match_count >= MAX_MATCHES) {
				fprintf(stderr, "Error: too many matches\n");
				return -EINVAL;
			}
			advance = get_match_arg(argc, argv, false, true,
					&matches[match_count], NULL);
			if (advance < 1)
				return -EINVAL;
			match_count++;
			for (; advance; advance--)
				next_arg();
		} else if (strcmp(*argv, "action") == 0) {
			struct net_mat_action a = {0};

			if (action_count >= MAX_ACTIONS) {
				fprintf(stderr, "Error: too many actions\n");
				return -EINVAL;
			}
			advance = get_action_arg(argc, argv, false, &a);
			if (advance < 0)
				return -EINVAL;
			acts[action_count] = a.uid;
			action_count++;
			for (; advance; advance--)
				next_arg();
			match_free_action(&a);
		} else if (strcmp(*argv, "attrib") == 0) {
			if (attrib_count >= MAX_ATTRIBS) {
				fprintf(stderr, "Error: too many attributes\n");
				return -EINVAL;
			}
			advance = get_attrib_arg(argc, argv, table.uid,
						 &attribs[attrib_count]);
			if (advance < 1)
				return -EINVAL;
			attrib_count++;
			for (; advance; advance--)
				next_arg();
		} else if (strcmp(*argv, "name") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing name\n");
				return -EINVAL;
			}

			table.name = strndup(*argv, NET_MAT_MAXNAME);
			if (!table.name) {
				fprintf(stderr, "Error: name too long\n");
				return -EINVAL;
			}
		} else if (strcmp(*argv, "source") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing source\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &table.source);
			if (err < 0) {
				fprintf(stderr, "Error: source invalid\n");
				return -EINVAL;
			}
		} else if (strcmp(*argv, "id") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing id\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &table.uid);
			if (err < 0) {
				fprintf(stderr, "Error: id invalid\n");
				return -EINVAL;
			}
		} else if (strcmp(*argv, "size") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing size\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &table.size);
			if (err < 0) {
				fprintf(stderr, "Error: size invalid\n");
				return -EINVAL;
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			if (cmd == NET_MAT_TABLE_CMD_CREATE_TABLE)
				create_usage();
			else
				update_usage();
			exit(-1);
		}
		argc--; argv++;
	}

	if (!table.uid && cmd == NET_MAT_TABLE_CMD_UPDATE_TABLE) {
		fprintf(stderr, "Error table id is required for update\n");
		update_usage();
		exit(-1);
	}

	if (!table.uid) {
		table.uid = gen_table_id();
		if (!table.uid) {
			fprintf(stderr, "Error: Cannot generate table id\n");
			return -EINVAL;
		}
	}

	if (!table.source && cmd == NET_MAT_TABLE_CMD_CREATE_TABLE) {
		fprintf(stderr, "Error: source is required\n");
		create_usage();
		exit(-1);
	}

	if (table.source && cmd == NET_MAT_TABLE_CMD_UPDATE_TABLE) {
		fprintf(stderr, "Error: source is not a valid argument\n");
		update_usage();
		exit(-1);
	}

	if (!table.name && cmd == NET_MAT_TABLE_CMD_CREATE_TABLE) {
		fprintf(stderr, "Error: name is required\n");
		create_usage();
		exit(-1);
	}

	if (!table.name && cmd == NET_MAT_TABLE_CMD_UPDATE_TABLE)
		table.name = table_names(table.uid);

	if (!table.size && cmd == NET_MAT_TABLE_CMD_CREATE_TABLE) {
		fprintf(stderr, "Error: size is required\n");
		create_usage();
		exit(-1);
	}

	if (table.size && cmd == NET_MAT_TABLE_CMD_UPDATE_TABLE) {
		fprintf(stderr, "Error: size can not be changed\n");
		update_usage();
		exit(-1);
	}

	if (!attrib_count && cmd == NET_MAT_TABLE_CMD_UPDATE_TABLE) {
		fprintf(stderr, "Error: no attributes specified for update\n");
		update_usage();
		exit(-1);
	}

	if (cmd == NET_MAT_TABLE_CMD_CREATE_TABLE &&
	    get_table_id(table.name)) {
		fprintf(stderr, "Error: table \"%s\" already exists\n",
			table.name);
		exit(-1);
	}

	pp_table(mat_stream_stdout(), &table);

	/* open generic netlink socket with match table api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMEM;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	nest = nla_nest_start(msg->nlbuf, NET_MAT_TABLES);
	if (!nest) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}
	nest1 = nla_nest_start(msg->nlbuf, NET_MAT_TABLE);
	match_put_table(msg->nlbuf, &table);
	nla_nest_end(msg->nlbuf, nest1);
	nla_nest_end(msg->nlbuf, nest);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);
	match_nl_free_msg(msg);
	return err;
}

int
rule_del_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
	int argc, char **argv)
{
	struct net_mat_rule rule = {0};
	struct match_msg *msg;
	struct nlattr *rules;
	uint8_t cmd = NET_MAT_TABLE_CMD_DEL_RULES;
	int err;

	while (argc > 0) {
		if (strcmp(*argv, "prio") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing prio\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &rule.priority);
			if (err < 0) {
				del_rule_usage();
				fprintf(stderr, "prio argument invalid\n");
				exit(-1);
			}
		} else if (strcmp(*argv, "handle") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing handle\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &rule.uid);
			if (err < 0) {
				del_rule_usage();
				fprintf(stderr, "handle argument invalid\n");
				exit(-1);
			}
		} else if (strcmp(*argv, "table") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing table\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &rule.table_id);
			if (err < 0) {
				del_rule_usage();
				fprintf(stderr, "table argument invalid\n");
				exit(-1);
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			del_rule_usage();
			exit(-1);
		}
		argc--; argv++;
	}

	if (!rule.table_id) {
		fprintf(stderr, "Table ID required\n");
		del_rule_usage();
		exit(-1);
	}

	if (!rule.uid) {
		fprintf(stderr, "Rule ID required\n");
		del_rule_usage();
		exit(-1);
	}

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMSG;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	err = match_put_rule_error(msg->nlbuf, NET_MAT_RULES_ERROR_ABORT);
	if (err) {
		match_nl_free_msg(msg);
		return err;
	}

	rules = nla_nest_start(msg->nlbuf, NET_MAT_RULES);
	if (!rules) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}
	match_put_rule(msg->nlbuf, &rule);
	nla_nest_end(msg->nlbuf, rules);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);

	match_nl_free_msg(msg);
	return err;
}

int
rule_get_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
	int argc, char **argv)
{
	uint8_t cmd = NET_MAT_TABLE_CMD_GET_RULES;
	unsigned int tableid, min = 0, max = 0;
	char *table = NULL;
	struct match_msg *msg;
	struct nlattr *rules;
	int err;

	opterr = 0;
	while (argc > 0) {
		if (strcmp(*argv, "table") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing table\n");
				return -EINVAL;
			}

			table = *argv;
		} else if (strcmp(*argv, "min") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing min\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &min);
			if (err < 0) {
				fprintf(stderr, "invalid min parameter\n");
				get_rules_usage();
				exit(-1);
			}
		} else if (strcmp(*argv, "max") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing max\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &max);
			if (err < 0) {
				fprintf(stderr, "invalid max parameter\n");
				get_rules_usage();
				exit(-1);
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			get_rules_usage();
			exit(-1);
		}
		argc--; argv++;
	}

	if  (!table) {
		printf("Missing \"table\" argument.\n");
		get_rules_usage();
		exit(-1);
	}

	err = sscanf(table, "%u", &tableid);
	if (err < 0) {
		tableid = find_table(table);
		if (!tableid) {
			printf("Missing \"table\" argument.\n");
			get_rules_usage();
			exit(-1);
		}
	}

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMSG;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	rules = nla_nest_start(msg->nlbuf, NET_MAT_RULES);
	if (!rules) {
		fprintf(stderr, "Error: get_rules attributes failed\n");
		match_nl_free_msg(msg);
		return -ENOMSG;
	}

	err = nla_put_u32(msg->nlbuf, NET_MAT_TABLE_RULES_TABLE, tableid);
	if (err) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	if (min > 0) {
		err = nla_put_u32(msg->nlbuf, NET_MAT_TABLE_RULES_MINPRIO,
				min);
		if (err) {
			match_nl_free_msg(msg);
			return err;
		}
	}

	if (max > 0) {
		err = nla_put_u32(msg->nlbuf, NET_MAT_TABLE_RULES_MAXPRIO,
				max);
		if (err) {
			match_nl_free_msg(msg);
			return err;
		}
	}


	nla_nest_end(msg->nlbuf, rules);

	err = match_put_rule_error(msg->nlbuf, NET_MAT_RULES_ERROR_ABORT);
	if (err) {
		match_nl_free_msg(msg);
		return err;
	}

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);

	match_nl_free_msg(msg);
	return err;
}

int
rule_set_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
	int argc, char **argv)
{
	struct net_mat_field_ref matches[MAX_MATCHES];
	struct net_mat_action acts[MAX_ACTIONS];
	int match_count = 0, action_count = 0;
	struct match_msg *msg;
	int advance = 0;
	uint8_t cmd = NET_MAT_TABLE_CMD_SET_RULES;
	int err = 0;
	struct net_mat_rule rule;
	struct nlattr *rules;
	const char *valid_keyword_list [] = {
		"match", "action", "prio", "handle", "table", NULL};

	memset(&rule, 0, sizeof(rule));
	memset(matches, 0, sizeof(struct net_mat_field_ref) * MAX_MATCHES);
	memset(acts, 0, sizeof(struct net_mat_action) * MAX_ACTIONS);
	rule.matches = &matches[0];
	rule.actions = &acts[0];

	opterr = 0;
	while (argc > 0) {
		if (strcmp(*argv, "match") == 0) {
			advance = get_match_arg(argc, argv, true, false,
					&matches[match_count], valid_keyword_list);
			if (advance < 0) {
				fprintf(stderr, "Error: invalid match argument\n");
				return -EINVAL;
			}
			match_count++;
			for (; advance; advance--)
				next_arg();
		} else if (strcmp(*argv, "action") == 0) {
			advance = get_action_arg(argc, argv, true,
					&acts[action_count]);
			if (advance < 0) {
				fprintf(stderr, "Error: invalid action argument\n");
				return -EINVAL;
			}
			action_count++;
			for (; advance; advance--)
				next_arg();
		} else if (strcmp(*argv, "prio") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing prio\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &rule.priority);
			if (err < 0) {
				printf("Invalid prio argument\n");
				set_rule_usage();
				exit(-1);
			}
		} else if (strcmp(*argv, "handle") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing handle\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &rule.uid);
			if (err < 0) {
				printf("Invalid handle argument\n");
				set_rule_usage();
				exit(-1);
			}
		} else if (strcmp(*argv, "table") == 0) {
			next_arg();
			err = sscanf(*argv, "%u", &rule.table_id);
			if (err < 0) {
				printf("Invalid table_id argument\n");
				set_rule_usage();
				exit(-1);
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			set_rule_usage();
			exit(-1);
		}
		argc--; argv++;
	}

	if (err < 0) {
		printf("Invalid argument\n");
		set_rule_usage();
		exit(-1);
	}

	if (!rule.table_id) {
		fprintf(stderr, "Table ID requried\n");
		set_rule_usage();
		exit(-1);
	}

	if (!rule.priority)
		rule.priority = 1;

	if (!rule.uid) {
		fprintf(stderr, "Rule ID required\n");
		set_rule_usage();
		exit(-1);
	}

	pp_rule(mat_stream_stdout(), &rule);

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMSG;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	err = match_put_rule_error(msg->nlbuf, NET_MAT_RULES_ERROR_ABORT);
	if (err) {
		match_nl_free_msg(msg);
		return err;
	}

	rules = nla_nest_start(msg->nlbuf, NET_MAT_RULES);
	if (!rules) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}
	match_put_rule(msg->nlbuf, &rule);
	nla_nest_end(msg->nlbuf, rules);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);

	match_nl_free_msg(msg);
	return err;
}

static int
match_get_port_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
		    int argc, char **argv, uint8_t cmd)
{
	bool have_pci_query = false;
	bool have_mac_query = false;
	bool have_lport_query = false;
	struct net_mat_port port;
	struct match_msg *msg;
	struct nlattr *nest0, *nest1;
	int ret, err;
	uint32_t min = NET_MAT_PORT_ID_UNSPEC, max = NET_MAT_PORT_ID_UNSPEC;

	memset(&port, 0, sizeof(port));
	port.port_id = NET_MAT_PORT_ID_UNSPEC;

	opterr = 0;
	while (argc > 0) {
		if (strcmp(*argv, "pci") == 0) {
			next_arg();

			if (*argv == NULL) {
				fprintf(stderr, "Error: missing function bus:device.function\n");
				return -EINVAL;
			}

			ret = sscanf(*argv, "%" SCNx8 ":%" SCNx8 ".%" SCNu8 "",
				     &port.pci.bus, &port.pci.device, &port.pci.function);

			if (ret != 3) {
				fprintf(stderr, "Error: pci device must be 'bus:device.function'\n");
				get_lport_usage();
				return -EINVAL;
			}
			have_pci_query = true;
		} else if (strcmp(*argv, "mac") == 0) {
			next_arg();

			if (*argv == NULL) {
				fprintf(stderr, "Error: missing mac address\n");
				return -EINVAL;
			}

			if (match_macaddr2u64(&port.mac_addr, *argv)) {
				fprintf(stderr,
					"Error: Invalid u64 or MAC address value (%s)\n",
					*argv);
				get_lport_usage();
				return -EINVAL;
			} else {
				/* one field was parsed */
				err = 1;
			}
			have_mac_query = true;
		} else if (strcmp(*argv, "lport") == 0) {
			next_arg();

			if (*argv == NULL) {
				fprintf(stderr, "Error: missing lport\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &port.port_id);
			if (err < 0) {
				fprintf(stderr, "invalid lport parameter\n");
				get_port_usage();
				exit(-1);
			}
			have_lport_query = true;
		} else if (strcmp(*argv, "min") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing min\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &min);
			if (err < 0) {
				fprintf(stderr, "invalid min parameter\n");
				get_port_usage();
				exit(-1);
			}
		} else if (strcmp(*argv, "max") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing max\n");
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &max);
			if (err < 0) {
				fprintf(stderr, "invalid max parameter\n");
				get_port_usage();
				exit(-1);
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			if (cmd == NET_MAT_PORT_CMD_GET_LPORT)
				get_lport_usage();
			else if (cmd == NET_MAT_PORT_CMD_GET_PHYS_PORT)
				get_phys_port_usage();
			else
				get_port_usage();

			exit(-1);
		}
		argc--; argv++;
	}

	if (cmd == NET_MAT_PORT_CMD_GET_LPORT) {
		if (!(have_pci_query ^ have_mac_query)) {
			fprintf(stderr, "Exactly one pci/mac argument is required\n");
			get_lport_usage();
			return -EINVAL;
		}
	} else if (cmd == NET_MAT_PORT_CMD_GET_PHYS_PORT) {
		if ((have_pci_query ^ have_mac_query)) {
			fprintf(stderr, "Unsupported pci/mac argument\n");
			get_phys_port_usage();
			return -EINVAL;
		} else if (!have_lport_query) {
			fprintf(stderr, "lport is required\n");
		}
	}

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMEM;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	nest0 = nla_nest_start(msg->nlbuf, NET_MAT_PORTS);
	if (!nest0) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	if (max != NET_MAT_PORT_ID_UNSPEC) {
		err = nla_put_u32(msg->nlbuf, NET_MAT_PORT_MAX_INDEX, max);
		if (err) {
			fprintf(stderr, "Error: put max port failed\n");
			match_nl_free_msg(msg);
			return -EMSGSIZE;
		}
	}

	if (min != NET_MAT_PORT_ID_UNSPEC) {
		err = nla_put_u32(msg->nlbuf, NET_MAT_PORT_MIN_INDEX, min);
		if (err) {
			fprintf(stderr, "Error: put max port failed\n");
			match_nl_free_msg(msg);
			return -EMSGSIZE;
		}
	}

	nest1 = nla_nest_start(msg->nlbuf, NET_MAT_PORT);
	if (!nest1) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	if (have_pci_query || have_mac_query || have_lport_query) {
		err = match_put_port(msg->nlbuf, &port);
		if (err) {
			fprintf(stderr, "Error: match put port failed\n");
			match_nl_free_msg(msg);
			return -EMSGSIZE;
		}
	}

	nla_nest_end(msg->nlbuf, nest1);
	nla_nest_end(msg->nlbuf, nest0);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);
	match_nl_free_msg(msg);
	return err;
}

static int
match_set_port_send(int verbose, uint32_t pid, int family, uint32_t ifindex,
		   int argc, char **argv, uint8_t cmd)
{
	struct nlattr *nest, *nest1;
	struct match_msg *msg;
	int err = 0;
	struct net_mat_port port;
	struct net_mat_port *port_be = NULL;
	bool have_vlans = false;

	memset(&port, 0, sizeof(port));
	port.vlan.def_priority = NET_MAT_PORT_T_DEF_PRI_UNSPEC;
	port.port_id = NET_MAT_PORT_ID_UNSPEC;

	opterr = 0;
	while (argc > 0) {
		if (strcmp(*argv, "port") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing port\n");
				set_port_usage();
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &port.port_id);
			if (err < 1) {
				fprintf(stderr, "Error: port invalid\n");
				set_port_usage();
				return -EINVAL;
			}

			/* Reading and caching existing vlan membership here so that it
			 * does not get reset when other settings for the port are changed.
			 */
			port_be = match_nl_get_ports(nsd, pid, 0, family, port.port_id, port.port_id);
			if (!port_be) {
				fprintf(stderr, "Error: match_nl_get_ports failed\n");
				return -EINVAL;
			}

		} else if (strcmp(*argv, "speed") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing speed\n");
				set_port_usage();
				return -EINVAL;
			}

			err = sscanf(*argv, "%" SCNu32 "", &port.speed);
			if (err < 1) {
				fprintf(stderr, "Error: speed invalid\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "max_frame_size") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing size\n");
				set_port_usage();
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &port.max_frame_size);
			if (err < 1) {
				fprintf(stderr, "Error: size invalid\n");
				set_port_usage();
				return -EINVAL;
			}
			if (port.max_frame_size == 0) {
				fprintf(stderr, "Error: max_frame_size cannnot be zero\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "state") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, "up") == 0) {
				port.state = NET_MAT_PORT_T_STATE_UP;
			} else if (strcmp(*argv, "down") == 0) {
				port.state = NET_MAT_PORT_T_STATE_DOWN;
			} else {
				fprintf(stderr, "Error: invalid state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "def_vlan") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing vlan\n");
				set_port_usage();
				return -EINVAL;
			}

			err = sscanf(*argv, "%u", &port.vlan.def_vlan);
			if (err < 1) {
				fprintf(stderr, "Error: vlan invalid\n");
				set_port_usage();
				return -EINVAL;
			}

			if (port.vlan.def_vlan < 1 || port.vlan.def_vlan > 4095) {
				fprintf(stderr, "Error: default VLAN must be in range [1..4095]\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "drop_tagged") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing priority\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.vlan.drop_tagged = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.vlan.drop_tagged = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid drop_tagged state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "drop_untagged") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing priority\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.vlan.drop_untagged = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.vlan.drop_untagged = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid drop_untagged state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "def_priority") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing priority\n");
				set_port_usage();
				return -EINVAL;
			}
			err = sscanf(*argv, "%u", &port.vlan.def_priority);
			if (err < 1) {
				fprintf(stderr, "Error: priority invalid\n");
				set_port_usage();
				return -EINVAL;
			}

			if (port.vlan.def_priority > 7) {
				fprintf(stderr, "Error: default VLAN priority must be in range [0..7]\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "vlans") == 0) {
			char *vlan;

			next_arg();
			vlan = strtok(*argv, ",");

			if (!vlan) {
				fprintf(stderr, "Error: missing `vlan` value\n");
				set_port_usage();
				return -EINVAL;
			}

			/* Zero out the cached vlan information since there is new membership
			 * being set now.
			 */
			memset(port.vlan.vlan_membership_bitmask, 0, (sizeof(__u8))*512);

			while (vlan) {
				int vid, slot;
				__u8 index;

				err = sscanf(vlan, "%i", &vid);
				if (err < 1 || vid > 4095 || vid < 0) {
					fprintf(stderr, "Error: invalid `vlan` input: %s\n", vlan);
					set_port_usage();
					return -EINVAL;
				}

				slot = (vid / 8);
				index = (__u8) (vid % 8);

				port.vlan.vlan_membership_bitmask[slot] |= (__u8) (1UL << index);
				vlan = strtok(NULL, ",");
			}

			have_vlans = true;

		} else if (strcmp(*argv, "loopback") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing loopback state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.loopback = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.loopback = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid loopback state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "learning") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing learning state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.learning = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.learning = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid learning state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "update_dscp") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing update_dscp state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.update_dscp = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.update_dscp = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid update_dscp state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "update_ttl") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing update_ttl state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.update_ttl = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.update_ttl = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid update_ttl state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "mcast_flooding") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing mcast_flooding state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.mcast_flooding = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.mcast_flooding = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid mcast_flooding state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "update_dmac") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing update_dmac state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.update_dmac = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.update_dmac = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid update_dmac state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "update_smac") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing update_smac state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.update_smac = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.update_smac = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid update_smac state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else if (strcmp(*argv, "update_vlan") == 0) {
			next_arg();
			if (*argv == NULL) {
				fprintf(stderr, "Error: missing update_vlan state\n");
				set_port_usage();
				return -EINVAL;
			}

			if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_ENABLED)) == 0) {
				port.update_vlan = NET_MAT_PORT_T_FLAG_ENABLED;
			} else if (strcmp(*argv, flag_state_str(NET_MAT_PORT_T_FLAG_DISABLED)) == 0) {
				port.update_vlan = NET_MAT_PORT_T_FLAG_DISABLED;
			} else {
				fprintf(stderr, "Error: invalid update_vlan state\n");
				set_port_usage();
				return -EINVAL;
			}
		} else {
			fprintf(stderr, "Error: unexpected argument `%s`\n", *argv);
			set_port_usage();
			exit(-1);
		}

		argc--; argv++;
	}

	if (port.port_id == NET_MAT_PORT_ID_UNSPEC) {
		fprintf(stderr, "Error port id is required\n");
		set_port_usage();
		exit(-1);
	}

	/* if vlans are not specified, re-use old vlan membership */
	if (!have_vlans)
		memcpy(port.vlan.vlan_membership_bitmask,
		       port_be->vlan.vlan_membership_bitmask,
		       sizeof(port.vlan.vlan_membership_bitmask));

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMEM;
	}

	if (nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
			NET_MAT_IDENTIFIER_IFINDEX) ||
	    nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex)) {
		fprintf(stderr, "Error: Identifier put failed\n");
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}

	nest = nla_nest_start(msg->nlbuf, NET_MAT_PORTS);
	if (!nest) {
		match_nl_free_msg(msg);
		return -EMSGSIZE;
	}
	nest1 = nla_nest_start(msg->nlbuf, NET_MAT_PORT);

	err = match_put_port(msg->nlbuf, &port);
	if (err) {
		fprintf(stderr, "Error: match put port failed\n");
		match_nl_free_msg(msg);
		return err;
	}

	nla_nest_end(msg->nlbuf, nest1);
	nla_nest_end(msg->nlbuf, nest);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);
	match_nl_free_msg(msg);

	/* Reading updated port settings from backend here to display to user */
	port_be = match_nl_get_ports(nsd, pid, 0, family, port.port_id, port.port_id);
	if (!port_be) {
		fprintf(stderr, "Error: match_nl_get_ports failed\n");
		return -EINVAL;
	}
	pp_port(mat_stream_stdout(), port_be);

	return err;
}
int
match_send_recv(int verbose, uint32_t pid, int family, uint32_t ifindex,
	uint8_t cmd)
{
	struct match_msg *msg;
	int err;

	/* open generic netlink socket with MATCH api */
	nsd = nl_socket_alloc();
	nl_connect(nsd, NETLINK_GENERIC);

	msg = match_nl_alloc_msg(cmd, pid, NLM_F_REQUEST|NLM_F_ACK, 0, family);
	if (!msg) {
		fprintf(stderr, "Error: Allocation failure\n");
		return -ENOMSG;
	}

	nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER_TYPE,
		NET_MAT_IDENTIFIER_IFINDEX);
	nla_put_u32(msg->nlbuf, NET_MAT_IDENTIFIER, ifindex);

	err = nl_send_auto(nsd, msg->nlbuf);
	if (err > 0)
		process_rx_message(verbose);
	else
		fprintf(stderr, "Error: nl_send_auto failed %i\n", err);

	match_nl_free_msg(msg);
	return err;
}

static int parse_arg_u32(char *argv, uint32_t *val)
{
	long ret;
	int base = 10;
	char *end = NULL;

	if (!argv || !val)
		return -1;

	if (strlen(argv) >= 2) {
		if (argv[0] == '0' && (argv[1] == 'x' || argv[1] == 'X'))
			base = 16;
	}

	ret = strtol(argv, &end, base);
	if (errno != 0 || ret < 0 || ret > ~((uint32_t)0) || argv == end)
		return -1;

	*val = (uint32_t)ret;

	return 0;
}

int main(int argc, char **argv)
{
	uint8_t cmd = NET_MAT_TABLE_CMD_GET_TABLES;
	int family = NET_MAT_DFLT_FAMILY, err;
	unsigned int ifindex = 0;
	uint32_t pid = 0;
	int verbose = 1;
	bool resolve_names = true;
	int opt;
	int args = 1;
	int opt_index = 0;
	static struct option long_options[] = {
		{ "version", no_argument, NULL, 0 },
		{ 0, 0, 0, 0}
	};

	progname = argv[0];
	if (argc < 2) {
		match_usage();
		return 0;
	}

	while ((opt = getopt_long(argc, argv, "p:f:hsg", long_options,
	                          &opt_index)) != -1) {
		switch (opt) {
		case 0:
			if (!strcmp(long_options[opt_index].name, "version")) {
				printf("Match Version: %s\n", match_version());
				exit(0);
			}
			break;
		case 'h':
			match_usage();
			exit(-1);
		case 'p':
			err = parse_arg_u32(optarg, &pid);
			if (err < 0) {
				fprintf(stderr, "Error parsing pid\n");
				match_usage();
				exit(-1);
			}
			args += 2;
			break;
		case 'f':
			family = atoi(optarg);
			args += 2;
			break;
		case 'g':
			verbose = PRINT_GRAPHVIZ;
			args++;
			break;
		case 's':
			verbose = 0;
			args++;
			break;
		default:
			match_usage();
			exit(-1);
		}
	}

	if (!pid) {
		pid = match_pid_lookup();
		if (!pid) {
			fprintf(stderr, "pid lookup failed and not specified\n");
			match_usage();
			exit(-1);
		}
	}

	/* argv[args] can be NULL if no CMD is provided */
	if (!argv[args]) {
		fprintf(stderr, "Error parsing command\n");
		match_usage();
		exit(-1);
	}

	if (strcmp(argv[args], "get_tables") == 0) {
		cmd = NET_MAT_TABLE_CMD_GET_TABLES;
	} else if (strcmp(argv[args], "get_headers") == 0) {
		resolve_names = false;
		cmd = NET_MAT_TABLE_CMD_GET_HEADERS;
	} else if (strcmp(argv[args], "get_header_graph") == 0) {
		cmd = NET_MAT_TABLE_CMD_GET_HDR_GRAPH;
	} else if (strcmp(argv[args], "get_actions") == 0) {
		resolve_names = false;
		cmd = NET_MAT_TABLE_CMD_GET_ACTIONS;
	} else if (strcmp(argv[args], "get_graph") == 0) {
		cmd = NET_MAT_TABLE_CMD_GET_TABLE_GRAPH;
	} else if (strcmp(argv[args], "get_rules") == 0) {
		cmd = NET_MAT_TABLE_CMD_GET_RULES;
		if (args + 1 >= argc) {
			get_rules_usage();
			return -1;
		}
	} else if (strcmp(argv[args], "set_rule") == 0) {
		cmd = NET_MAT_TABLE_CMD_SET_RULES;
	} else if (strcmp(argv[args], "del_rule") == 0) {
		cmd = NET_MAT_TABLE_CMD_DEL_RULES;
	} else if (strcmp(argv[args], "create") == 0) {
		cmd = NET_MAT_TABLE_CMD_CREATE_TABLE;
	} else if (strcmp(argv[args], "destroy") == 0) {
		cmd = NET_MAT_TABLE_CMD_DESTROY_TABLE;
	} else if (strcmp(argv[args], "update") == 0) {
		cmd = NET_MAT_TABLE_CMD_UPDATE_TABLE;
	} else if (strcmp(argv[args], "phys_port_lookup") == 0) {
		cmd = NET_MAT_PORT_CMD_GET_PHYS_PORT;
	} else if (strcmp(argv[args], "lport_lookup") == 0) {
		cmd = NET_MAT_PORT_CMD_GET_LPORT;
	} else if (strcmp(argv[args], "get_ports") == 0) {
		cmd = NET_MAT_PORT_CMD_GET_PORTS;
	} else if (strcmp(argv[args], "set_port") == 0) {
		cmd = NET_MAT_PORT_CMD_SET_PORTS;
	} else {
		match_usage();
		err = -EINVAL;
		goto out;
	}

	/* Get the family */
	if (family < 0) {
		struct nl_sock *fd = NULL;

		fd = nl_socket_alloc();
		genl_connect(fd);

		family = genl_ctrl_resolve(fd, NET_MAT_GENL_NAME);
		if (family < 0) {
			printf("Can not resolve family\n");
			goto out;
		}
		nl_close(fd);
		nl_socket_free(fd);
	}

	if (resolve_names) {
		err = match_send_recv(0, pid, family, ifindex,
				NET_MAT_TABLE_CMD_GET_HEADERS);
		if (err < 0)
			goto out;
		err = match_send_recv(0, pid, family, ifindex,
				NET_MAT_TABLE_CMD_GET_ACTIONS);
		if (err < 0)
			goto out;
		err = match_send_recv(0, pid, family, ifindex,
				NET_MAT_TABLE_CMD_GET_TABLES);
		if (err < 0)
			goto out;
		err = match_send_recv(0, pid, family, ifindex,
				NET_MAT_TABLE_CMD_GET_HDR_GRAPH);
		if (err < 0)
			goto out;
	}

	/* strip progname, options, and command from the command line */
	argc = argc - args - 1;
	argv = argv + args + 1;

	switch (cmd) {
	case NET_MAT_TABLE_CMD_SET_RULES:
		rule_set_send(verbose, pid, family, ifindex, argc, argv);
		break;
	case NET_MAT_TABLE_CMD_DEL_RULES:
		rule_del_send(verbose, pid, family, ifindex, argc, argv);
		break;
	case NET_MAT_TABLE_CMD_GET_RULES:
		rule_get_send(verbose, pid, family, ifindex, argc, argv);
		break;
	case NET_MAT_TABLE_CMD_CREATE_TABLE:
		match_create_tbl_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	case NET_MAT_TABLE_CMD_DESTROY_TABLE:
		match_destroy_tbl_send(verbose, pid, family, ifindex,
				argc, argv);
		break;
	case NET_MAT_TABLE_CMD_UPDATE_TABLE:
		match_create_tbl_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	case NET_MAT_PORT_CMD_GET_LPORT:
		match_get_port_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	case NET_MAT_PORT_CMD_GET_PHYS_PORT:
		match_get_port_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	case NET_MAT_PORT_CMD_GET_PORTS:
		match_get_port_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	case NET_MAT_PORT_CMD_SET_PORTS:
		match_set_port_send(verbose, pid, family, ifindex, argc, argv, cmd);
		break;
	default:
		match_send_recv(verbose, pid, family, ifindex, cmd);
		break;
	}
out:
	return 0;
}
