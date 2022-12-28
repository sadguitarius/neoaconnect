/*
 * connect / disconnect two subscriber ports
 *   ver.0.1
 *
 * Copyright (C) 2022 Ben Goldwasser based on aconnect by Takashi Iwai
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <iostream>
#include <vector>
#include <string>
#include <charconv>
#include <getopt.h>
#include <fmt/core.h>
#include <alsa/asoundlib.h>
#include <toml++/toml.h>
#include <regex>
#include <chrono>
#include <thread>

struct Connection {
    int client_id_;
    int port_id_;
    std::string client_name_;
    std::string port_name_;
};

class Port {
   public:
    Port(snd_seq_t *seq, int client_id, std::string client_name, int index,
         std::string name, unsigned int capability)
        : seq_(seq),
          client_id_(client_id),
          client_name_(client_name),
          index_(index),
          name_(name),
          capability_(capability) {
        populate_connections();
    }

    const int get_client_id() { return client_id_; }

    const std::string get_client_name() { return client_name_; }

    const int get_index() { return index_; }

    const std::string get_name() { return name_; }

    unsigned int get_capability() { return capability_; }

    std::vector<Connection> get_connections() { return connections_; }

   private:
    snd_seq_t *seq_;
    int client_id_;
    std::string client_name_;
    int index_;
    std::string name_;
    unsigned int capability_;
    std::vector<Connection> connections_;

    void populate_connections() {
        snd_seq_addr_t addr;
        addr.client = client_id_;
        addr.port = index_;
        snd_seq_query_subscribe_t *subs;
        snd_seq_query_subscribe_alloca(&subs);
        snd_seq_query_subscribe_set_root(subs, &addr);
        snd_seq_query_subscribe_set_type(subs, SND_SEQ_QUERY_SUBS_READ);
        snd_seq_query_subscribe_set_index(subs, 0);
        while (snd_seq_query_port_subscribers(seq_, subs) >= 0) {
            const snd_seq_addr_t *subs_addr;
            subs_addr = snd_seq_query_subscribe_get_addr(subs);
            snd_seq_port_info_t *pinfo;
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_client_info_t *cinfo;
            snd_seq_client_info_alloca(&cinfo);
            snd_seq_get_any_port_info(seq_, subs_addr->client, subs_addr->port,
                                      pinfo);
            snd_seq_get_any_client_info(seq_, subs_addr->client, cinfo);
            connections_.push_back({subs_addr->client, subs_addr->port,
                                    snd_seq_client_info_get_name(cinfo),
                                    snd_seq_port_info_get_name(pinfo)});
            snd_seq_query_subscribe_set_index(
                subs, snd_seq_query_subscribe_get_index(subs) + 1);
        }
    }
};

class Client {
   public:
    Client(snd_seq_t *seq, int index, std::string name,
           snd_seq_client_type type)
        : seq_(seq), index_(index), name_(name), type_(type) {
        populate_ports();
    }

    const int get_index() { return index_; }

    const std::string get_name() { return name_; }

    const snd_seq_client_type get_type() { return type_; }

    const std::vector<Port *> *get_ports() { return &ports_; };

   private:
    snd_seq_t *seq_;
    int index_;
    std::string name_;
    snd_seq_client_type type_;
    std::vector<Port *> ports_;

    void populate_ports() {
        snd_seq_port_info_t *pinfo;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, index_);
        snd_seq_port_info_set_port(pinfo, -1);

        while (snd_seq_query_next_port(seq_, pinfo) >= 0) {
            int client_id = index_;
            int index = snd_seq_port_info_get_port(pinfo);
            std::string name = snd_seq_port_info_get_name(pinfo);
            unsigned int capability = snd_seq_port_info_get_capability(pinfo);
            ports_.push_back(
                new Port(seq_, client_id, name_, index, name, capability));
        }
    };
};

class Seq {
   public:
    using Clients = std::vector<Client *>;
    enum permission : int { LIST_INPUT = 1, LIST_OUTPUT = 2 };
    // #define SND_SEQ_PORT_CAP_READ		(1<<0)	/**< readable from this
    // port
    // */ #define SND_SEQ_PORT_CAP_WRITE		(1<<1)	/**< writable to
    // this port */
    //
    // #define SND_SEQ_PORT_CAP_SYNC_READ	(1<<2)	/**< allow read
    // subscriptions
    // */ #define SND_SEQ_PORT_CAP_SYNC_WRITE	(1<<3)	/**< allow write
    // subscriptions */
    //
    // #define SND_SEQ_PORT_CAP_DUPLEX		(1<<4)	/**< allow read/write
    // duplex
    // */
    //
    // #define SND_SEQ_PORT_CAP_SUBS_READ	(1<<5)	/**< allow read
    // subscription
    // */ #define SND_SEQ_PORT_CAP_SUBS_WRITE	(1<<6)	/**< allow write
    // subscription */
    // #define SND_SEQ_PORT_CAP_NO_EXPORT	(1<<7)	/**< routing not allowed
    // */

    Seq() {
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
            std::cerr << "can't open sequencer\n";
        }

        snd_lib_error_set_handler(error_handler);

        populate_clients();
    }

    ~Seq() { snd_seq_close(seq); }

    void populate_clients() {
        snd_seq_client_info_t *cinfo;
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_client_info_set_client(cinfo, -1);
        while (snd_seq_query_next_client(seq, cinfo) >= 0) {
            int index = snd_seq_client_info_get_client(cinfo);
            std::string name = snd_seq_client_info_get_name(cinfo);
            snd_seq_client_type type = snd_seq_client_info_get_type(cinfo);
            clients.push_back(new Client(seq, index, name, type));
        }
    };

    std::vector<Client *> *get_clients() { return &clients; }

    Clients::iterator begin() { return clients.begin(); };

    Clients::iterator end() { return clients.end(); };

    void print_list(int list_perm, bool list_subs) {
        // TODO: reintroduce card info
        for (auto client : *get_clients()) {
            std::cout << "client " << client->get_index() << ": '"
                      << client->get_name() << "' [type="
                      << (client->get_type() == SND_SEQ_USER_CLIENT ? "user"
                                                                    : "kernel")
                      << "]\n";

            for (auto port : *client->get_ports()) {
                print_port(port);
                for (auto conn : port->get_connections()) {
                    std::cout << "    -> " << conn.client_id_ << ":"
                              << conn.port_id_ << " (" << conn.client_name_
                              << ":" << conn.port_name_ << ")\n";
                }
                for (auto testport : *client->get_ports()) {
                    for (auto testconn : testport->get_connections()) {
                        if (testconn.client_id_ == port->get_client_id() &&
                            testconn.port_id_ == port->get_index()) {
                            std::cout << "    <- " << testport->get_client_id()
                                      << ":" << testport->get_index() << " ("
                                      << testport->get_client_name() << ":"
                                      << testport->get_name() << ")\n";
                        }
                    }
                }
            }
        }
    }

    void print_all_ports(int list_perm, bool list_subs) {
        for (auto client : *get_clients()) {
            for (auto port : *client->get_ports()) {
                std::cout << ":'" << port->get_name() << "'\n";
            }
        }
    }

    int subscribe(const char *send_address, const char *dest_address,
                  int queue = 0, int exclusive = 0, int convert_time = 0,
                  int convert_real = 0) {
        snd_seq_port_subscribe_t *subs;
        snd_seq_port_subscribe_alloca(&subs);

        if (init_subscription(subs, send_address, dest_address, queue,
                              exclusive, convert_time, convert_real) != 0) {
            return 1;
        }

        if (snd_seq_get_port_subscription(seq, subs) == 0) {
            std::cerr << "connection is already subscribed\n";
            return 1;
        }

        if (snd_seq_subscribe_port(seq, subs) < 0) {
            std::cerr << "connection failed (" << snd_strerror(errno) << ")\n";
            return 1;
        }

        return 0;
    };

    int unsubscribe(char *send_address, char *dest_address, int queue = 0,
                    int exclusive = 0, int convert_time = 0,
                    int convert_real = 0) {
        snd_seq_port_subscribe_t *subs;
        snd_seq_port_subscribe_alloca(&subs);

        if (init_subscription(subs, send_address, dest_address, queue,
                              exclusive, convert_time, convert_real) != 0) {
            return 1;
        }

        if (snd_seq_get_port_subscription(seq, subs) < 0) {
            std::cerr << "no subscription is found\n";
            return 1;
        }

        if (snd_seq_unsubscribe_port(seq, subs) < 0) {
            std::cerr << "disconnection failed (" << snd_strerror(errno)
                      << ")\n";
            return 1;
        }

        return 0;
    };

    /*
     * remove all (exported) connections
     */
    void remove_connection(Port *p) {
        snd_seq_port_info_t *pinfo;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, p->get_client_id());
        snd_seq_port_info_set_port(pinfo, p->get_index());
        snd_seq_query_subscribe_t *query;
        snd_seq_port_info_t *port;
        snd_seq_port_subscribe_t *subs;

        snd_seq_query_subscribe_alloca(&query);
        snd_seq_query_subscribe_set_root(query,
                                         snd_seq_port_info_get_addr(pinfo));
        snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_READ);
        snd_seq_query_subscribe_set_index(query, 0);

        snd_seq_port_info_alloca(&port);
        snd_seq_port_subscribe_alloca(&subs);

        while (snd_seq_query_port_subscribers(seq, query) >= 0) {
            const snd_seq_addr_t *sender =
                snd_seq_query_subscribe_get_root(query);
            const snd_seq_addr_t *dest =
                snd_seq_query_subscribe_get_addr(query);

            if (snd_seq_get_any_port_info(seq, dest->client, dest->port, port) <
                    0 ||
                !(snd_seq_port_info_get_capability(port) &
                  SND_SEQ_PORT_CAP_SUBS_WRITE) ||
                (snd_seq_port_info_get_capability(port) &
                 SND_SEQ_PORT_CAP_NO_EXPORT)) {
                snd_seq_query_subscribe_set_index(
                    query, snd_seq_query_subscribe_get_index(query) + 1);
                continue;
            }
            snd_seq_port_subscribe_set_queue(
                subs, snd_seq_query_subscribe_get_queue(query));
            snd_seq_port_subscribe_set_sender(subs, sender);
            snd_seq_port_subscribe_set_dest(subs, dest);
            if (snd_seq_unsubscribe_port(seq, subs) < 0) {
                snd_seq_query_subscribe_set_index(
                    query, snd_seq_query_subscribe_get_index(query) + 1);
            }
        }
    }

    void remove_all_connections() {
        for (auto client : *get_clients()) {
            for (auto port : *client->get_ports()) {
                remove_connection(port);
            }
        }
    }

    void serialize_connections() {
        auto tbl = toml::table();

        for (auto client : clients) {
            // auto port_tbl = toml::table();
            toml::table ports_tbl;
            for (auto port : *client->get_ports()) {
                auto connections = port->get_connections();
                toml::array conn_arr;
                for (auto conn : connections) {
                    conn_arr.push_back(fmt::format("{}:{}", conn.client_name_,
                                                   conn.port_name_));
                }
                if (!conn_arr.empty()) {
                    ports_tbl.emplace(port->get_name(), conn_arr);
                }
            }
            if (!ports_tbl.empty()) {
                tbl.emplace(client->get_name(), ports_tbl);
            }
        }

        std::cout << tbl << "\n";
    }

    void deserialize_connections(char *filename, bool remove_prev = true) {
        using namespace std::chrono_literals;

        toml::table tbl;
        try {
            tbl = toml::parse_file(filename);
            // std::cout << tbl << "\n";
        } catch (const toml::parse_error &err) {
            std::cerr << "TOML parsing failed:\n" << err << "\n";
        }
        if (remove_prev) {
            remove_all_connections();
        }
        for (auto client : tbl) {
            auto client_name = std::string(client.first);
            auto ports = client.second.as_table();
            for (auto port : *ports) {
                auto port_name = std::string(port.first);
                auto connections = port.second.as_array();
                auto send_addr = fmt::format("{}:{}", client_name, port_name);
                connections->for_each([&](toml::value<std::string> &elem) {
                    subscribe(send_addr.c_str(), elem->c_str());
                    std::this_thread::sleep_for(0.5s);
                });
            }
        };
    }

   private:
    snd_seq_t *seq;
    std::vector<Client *> clients;

    static void error_handler(const char *file, int line, const char *function,
                              int err, const char *fmt, ...) {
        va_list arg;

        if (err == ENOENT) /* Ignore those misleading "warnings" */
            return;
        va_start(arg, fmt);
        fprintf(stderr, "ALSA lib %s:%i:(%s) ", file, line, function);
        vfprintf(stderr, fmt, arg);
        if (err) fprintf(stderr, ": %s", snd_strerror(err));
        putc('\n', stderr);
        va_end(arg);
    }

    int parse_address(snd_seq_addr_t *addr, const std::string arg) {
        std::string arg_client_name, arg_port_name;
        Client *client;
        Port *port;
        int arg_client_id = -1;
        int arg_port_id = -1;

        assert(arg.length());

        const std::regex addr_regex_full(
            "[\'\"]?([^:\\.]+)?[\'\"]?[:\\.]?[\'\"]?([^:\\.]+)?[\'\"]?");
        std::smatch addr_parts;

        // match client and port
        if (std::regex_match(arg, addr_parts, addr_regex_full)) {
            arg_client_name = addr_parts[1].str();
            arg_port_name = addr_parts[2].str();
            // std::cout << "\ngot client: " << arg_client_name
            //           << ", port: " << arg_port_name << "\n";

            if (arg_client_name.length() != 0)
            // client name or number was provided
            {
                // std::cout << "client name or number was provided\n";
                // try to parse number from client string
                auto cname_ptr = arg_client_name.data();
                int parsed_client_id;

                const auto client_iconv = std::from_chars(
                    cname_ptr, cname_ptr + arg_client_name.size(),
                    parsed_client_id);

                if (client_iconv.ec == std::errc()) {
                    // number was found
                    // std::cout << "parsed number " << parsed_client_id
                    // << " from client argument\n";
                    arg_client_id = parsed_client_id;
                    for (auto client_it : clients) {
                        if (arg_client_id == client_it->get_index()) {
                            // std::cout << "matched client id!\n";
                            client = client_it;
                            break;
                        }
                    }
                } else {
                    // number not found, interpret as string
                    // std::cout
                    // << "client number not found, interpreting as string\n";
                    for (auto client_it : clients) {
                        if (arg_client_name == client_it->get_name()) {
                            // std::cout << "matched client name!\n";
                            client = client_it;
                            break;
                        }
                    }
                }

                if (client == NULL) {
                    // client not found
                    std::cout << "invalid client entry\n";
                    return -ENOENT;
                }

                // try to parse number from port string
                auto pname_ptr = arg_port_name.data();
                int parsed_port_id;

                const auto port_iconv =
                    std::from_chars(pname_ptr, pname_ptr + arg_port_name.size(),
                                    parsed_port_id);

                if (arg_port_name.size() == 0) {
                    // port not provided
                    port = client->get_ports()->front();
                    addr->client = client->get_index();
                    addr->port = port->get_index();
                    return 0;
                }

                if (port_iconv.ec == std::errc() || arg_port_name.size() == 0) {
                    // number was found
                    // std::cout << "parsed number " << parsed_port_id
                    //           << " from port argument\n";
                    arg_port_id = parsed_port_id;
                    for (auto port : *client->get_ports()) {
                        if (arg_port_id == port->get_index()) {
                            // std::cout << "matched port id!\n";
                            addr->client = client->get_index();
                            addr->port = port->get_index();
                            return 0;
                        }
                    }
                } else {
                    // number not found, interpret as string
                    // std::cout
                    //     << "port number not found, interpreting as string: "
                    //     << arg_port_name << "\n";
                    for (auto port : *client->get_ports()) {
                        if (arg_port_name == port->get_name()) {
                            // std::cout << "matched port name!\n";
                            addr->client = client->get_index();
                            addr->port = port->get_index();
                            return 0;
                        }
                    }
                }
            } else
            // client name was not provided, search for port name in all clients
            {
                for (auto client_it : clients) {
                    for (auto port : *client_it->get_ports()) {
                        if (arg_port_name == port->get_name()) {
                            // std::cout << "matched port name!\n";
                            addr->client = client_it->get_index();
                            addr->port = port->get_index();
                            return 0;
                        }
                    }
                }
            }
        }

        return -ENOENT;
    }

    inline static bool perm_ok(Port *p, unsigned int bits) {
        return ((p->get_capability() & bits) == (bits));
    }

    static int check_permission(Port *p, unsigned int perm) {
        if (perm) {
            if (perm & LIST_INPUT) {
                if (perm_ok(p,
                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                    goto __ok;
            }
            if (perm & LIST_OUTPUT) {
                if (perm_ok(p, SND_SEQ_PORT_CAP_WRITE |
                                   SND_SEQ_PORT_CAP_SUBS_WRITE))
                    goto __ok;
            }
            return 0;
        }
    __ok:
        if (p->get_capability() & SND_SEQ_PORT_CAP_NO_EXPORT) return 0;
        return 1;
    }

    /*
     * list subscribers
     */
    void list_subscribers(Port *port) {
        snd_seq_addr_t addr;
        addr.client = port->get_client_id();
        addr.port = port->get_index();
        snd_seq_query_subscribe_t *subs;
        snd_seq_query_subscribe_alloca(&subs);
        snd_seq_query_subscribe_set_root(subs, &addr);
        list_each_subs(subs, SND_SEQ_QUERY_SUBS_READ, "Connecting To");
        list_each_subs(subs, SND_SEQ_QUERY_SUBS_WRITE, "Connected From");
    }

    /*
     * list subscribers of specified type
     */
    void list_each_subs(snd_seq_query_subscribe_t *subs,
                        snd_seq_query_subs_type_t type, const char *msg) {
        int count = 0;
        snd_seq_query_subscribe_set_type(subs, type);
        snd_seq_query_subscribe_set_index(subs, 0);
        while (snd_seq_query_port_subscribers(seq, subs) >= 0) {
            const snd_seq_addr_t *addr;
            if (count++ == 0)
                printf("\t%s: ", msg);
            else
                printf(", ");
            addr = snd_seq_query_subscribe_get_addr(subs);
            printf("%d:%d", addr->client, addr->port);
            if (snd_seq_query_subscribe_get_exclusive(subs)) printf("[ex]");
            if (snd_seq_query_subscribe_get_time_update(subs))
                printf("[%s:%d]",
                       (snd_seq_query_subscribe_get_time_real(subs) ? "real"
                                                                    : "tick"),
                       snd_seq_query_subscribe_get_queue(subs));
            snd_seq_query_subscribe_set_index(
                subs, snd_seq_query_subscribe_get_index(subs) + 1);
        }
        if (count > 0) printf("\n");
    }

    /*
     * search all ports
     */
    void print_port(Port *port) {
        fmt::print("  {:<3} '{}'\n", port->get_index(), port->get_name());
    }

    void print_port_and_subs(Port *port) {
        snd_seq_port_info_t *pinfo;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, port->get_client_id());
        snd_seq_port_info_set_port(pinfo, port->get_index());
        print_port(port);
        list_subscribers(port);
    }

    int init_subscription(snd_seq_port_subscribe_t *subs,
                          const char *send_address, const char *dest_address,
                          int queue = 0, int exclusive = 0,
                          int convert_time = 0, int convert_real = 0) {
        int client;
        snd_seq_addr_t sender, dest;

        if ((client = snd_seq_client_id(seq)) < 0) {
            std::cerr << "can't get client id\n";
            return 1;
        }

        /* set client info */
        if (snd_seq_set_client_name(seq, "ALSA Connector") < 0) {
            std::cerr << "can't set client info\n";
            return 1;
        }

        if (parse_address(&sender, send_address) < 0) {
            std::cerr << "invalid sender address '" << send_address << "'\n";
            return 1;
        }

        if (parse_address(&dest, dest_address) < 0) {
            std::cerr << "invalid destination address '" << dest_address << "'\n";
            return 1;
        }

        snd_seq_port_subscribe_set_sender(subs, &sender);
        snd_seq_port_subscribe_set_dest(subs, &dest);
        snd_seq_port_subscribe_set_queue(subs, queue);
        snd_seq_port_subscribe_set_exclusive(subs, exclusive);
        snd_seq_port_subscribe_set_time_update(subs, convert_time);
        snd_seq_port_subscribe_set_time_real(subs, convert_real);

        return 0;
    }
};

static void usage(void) {
    std::cout
        << "neoaconnect - ALSA sequencer connection manager\n"
           "Copyright (C) 2022 Ben Goldwasser\n"
           "based on aconnect by Takashi Iwai\n"
           "Usage:\n"
           " * Connection/disconnection between two ports\n"
           "   neoaconnect [-options] sender receiver\n"
           "     sender, receiver = client:port pair\n"
           "     -d,--disconnect     disconnect\n"
           "     -e,--exclusive      exclusive connection\n"
           "     -r,--real #         convert real-time-stamp on queue\n"
           "     -t,--tick #         convert tick-time-stamp on queue\n"
           " * List connected ports (no subscription action)\n"
           "   neoaconnect -i|-o [-options]\n"
           "     -i,--input          list input (readable ports)\n"
           "     -o,--output         list output (writable ports)\n"
           "     -l,--list           list current connections of each port\n"
           "     -p,--ports          list only port names \n"
           "                         (for shell completion scripts)"
           " * Remove all exported connections\n"
           "     -x,--removeall\n"
           " * Serialization of connections in TOML format\n"
           "    -s,--serialize      read current connections to terminal\n"
           "    -S FILENAME,\n"
           "--deserialize    repopulate connections from TOML file\n";
}

/*
 * main..
 */

static const struct option long_option[] = {
    {"disconnect", 0, NULL, 'd'},  {"input", 0, NULL, 'i'},
    {"output", 0, NULL, 'o'},      {"real", 1, NULL, 'r'},
    {"tick", 1, NULL, 't'},        {"exclusive", 0, NULL, 'e'},
    {"list", 0, NULL, 'l'},        {"ports", 0, NULL, 'p'},
    {"removeall", 0, NULL, 'x'},   {"serialize", 0, NULL, 's'},
    {"deserialize", 0, NULL, 'S'}, {NULL, 0, NULL, 0},
};

int main(int argc, char **argv) {
    enum commands {
        subscribe,
        unsubscribe,
        list,
        ports,
        remove_all,
        serialize,
        deserialize
    };

    std::unique_ptr<Seq> seq = std::make_unique<Seq>();
    int c;
    int command = subscribe;
    int list_perm = 0;
    int list_subs = 0;
    int queue = 0, convert_time = 0, convert_real = 0, exclusive = 0;

    // CHANGE TO CLASS METHODS
    while ((c = getopt_long(argc, argv, "dior:t:elpsSx", long_option, NULL)) !=
           -1) {
        switch (c) {
            case 'd':
                command = commands::unsubscribe;
                break;
            case 'e':
                exclusive = 1;
                break;
            case 'r':
                queue = atoi(optarg);
                convert_time = 1;
                convert_real = 1;
                break;
            case 's':
                command = commands::serialize;
                break;
            case 'S':
                command = commands::deserialize;
                break;
            case 't':
                queue = atoi(optarg);
                convert_time = 1;
                convert_real = 0;
                break;
            case 'l':
                command = commands::list;
                list_subs = 1;
                break;
            case 'p':
                command = commands::ports;
                list_subs = 1;
                break;
            case 'i':
                // command = commands::list;
                list_perm |= Seq::LIST_INPUT;
                break;
            case 'o':
                // command = commands::list;
                list_perm |= Seq::LIST_OUTPUT;
                break;
            case 'x':
                command = commands::remove_all;
                break;
            default:
                usage();
                exit(1);
        }
    }

    switch (command) {
        case commands::list:
            seq->print_list(list_perm, list_subs);
            return 0;
        case commands::ports:
            seq->print_all_ports(list_perm, list_subs);
            return 0;
        case commands::remove_all:
            seq->remove_all_connections();
            return 0;
        case commands::serialize:
            seq->serialize_connections();
            return 0;
        case commands::deserialize:
            if (optind + 1 > argc) {
                usage();
                exit(1);
            }
            seq->deserialize_connections(argv[optind]);
            return 0;
    }

    /* connection or disconnection */

    if (optind + 2 > argc) {
        usage();
        exit(1);
    }

    if (command == commands::unsubscribe) {
        seq->unsubscribe(argv[optind], argv[optind + 1], queue, exclusive,
                         convert_time, convert_real);
    } else {
        seq->subscribe(argv[optind], argv[optind + 1], queue, exclusive,
                       convert_time, convert_real);
    }

    return 0;
}
