#ifndef __RPC_CONNECTIVITY_CONNECTIVITY_HPP__
#define __RPC_CONNECTIVITY_CONNECTIVITY_HPP__

#include <map>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/optional.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/utility.hpp>
#include "arch/streamed_tcp.hpp"
#include "arch/address.hpp"
#include "utils.hpp"
#include "concurrency/signal.hpp"
#include "concurrency/mutex.hpp"

namespace connectivity {

struct address_t {
    address_t(ip_address_t i, int p) : ip(i), port(p) { }
    address_t() : ip(), port(0) { } // For deserialization
    ip_address_t ip;
    int port;

    bool operator==(const address_t &a) const {
        return ip == a.ip && port == a.port;
    }
    bool operator!=(const address_t &a) const {
        return ip != a.ip || port != a.port;
    }

private:
    friend class ::boost::serialization::access;
    template<class Archive> void serialize(Archive & ar, UNUSED const unsigned int version) {
        ar & ip;
        ar & port;
    }
};

struct cluster_t :
    public home_thread_mixin_t
{
public:

    /* `peer_id_t` is a wrapper around a `boost::uuids::uuid`. Each newly
    created cluster node picks a UUID to be its peer-ID. */
    struct peer_id_t {
        bool operator==(const peer_id_t &p) const {
            return p.uuid == uuid;
        }
        bool operator!=(const peer_id_t &p) const {
            return p.uuid != uuid;
        }
        bool operator<(const peer_id_t &p) const {
            return p.uuid < uuid;
        }

        // TODO: This should not be used. Can we get rid of it without breaking
        // the possibility of using this in an std::map and for deserialization?
        // (other than making all required classes friends and this private)
        peer_id_t() { }
    private:
        friend class cluster_t;
        boost::uuids::uuid uuid;
        peer_id_t(boost::uuids::uuid u) : uuid(u) { }

        friend class boost::serialization::access;
        template<class Archive> void serialize(Archive & ar, UNUSED const unsigned int version) {
            // Not really platform independent...
            boost::serialization::binary_object bin_uuid(&uuid, boost::uuids::uuid::static_size());
            ar & bin_uuid;
        }
    };

    /* Creating a new cluster node, and connecting one cluster to another
    cluster */
    cluster_t(int port);
    cluster_t(int port, peer_id_t id_from_last_time);
    ~cluster_t();
    void join(address_t);

    /* `get_me()` returns the `peer_id_t` for this cluster node.
    `get_everybody()` returns all the currently-accessible peers in the
    cluster and their addresses, including us. */
    peer_id_t get_me();
    std::map<peer_id_t, address_t> get_everybody();

    /* `event_watcher_t` is used to watch for any node joining or leaving the
    cluster. `connect_watcher_t` and `disconnect_watcher_t` are used to watch
    for a specific peer connecting or disconnecting. */
    struct event_watcher_t : public intrusive_list_node_t<event_watcher_t> {
        event_watcher_t(cluster_t *);
        virtual ~event_watcher_t();
        virtual void on_connect(peer_id_t) = 0;
        virtual void on_disconnect(peer_id_t) = 0;
    private:
        cluster_t *cluster;
    };
    struct connect_watcher_t : public signal_t, private event_watcher_t {
        connect_watcher_t(cluster_t *, peer_id_t);
    private:
        void on_connect(peer_id_t);
        void on_disconnect(peer_id_t);
        peer_id_t peer;
    };
    struct disconnect_watcher_t : public signal_t, private event_watcher_t {
        disconnect_watcher_t(cluster_t *, peer_id_t);
    private:
        void on_connect(peer_id_t);
        void on_disconnect(peer_id_t);
        peer_id_t peer;
    };

    /* `send_message()` is used to send a message to a specific peer. The
    function will be called with a `std::ostream&` that leads to the peer in
    question. */
    void send_message(peer_id_t, boost::function<void(std::ostream&)>);

    /* `on_message()` is called every time we receive a message. It's called
    with a `std::istream&` that comes from the peer in question. */
    virtual void on_message(peer_id_t, std::istream&) = 0;

private:
    /* We are always listening for new connections from other peers. */
    streamed_tcp_listener_t listener;
    void on_new_connection(streamed_tcp_conn_t *);

    void handle(streamed_tcp_conn_t *c,
        boost::optional<peer_id_t> expected_id,
        boost::optional<address_t> expected_address,
        drain_semaphore_t::lock_t drain_semaphore_lock);

    /* `me` is our `peer_id_t`. `routing_table` is all the peers we can
    currently access and their addresses. Peers that are in the process of
    connecting or disconnecting may be in `routing_table` but not in
    `connections`. */
    peer_id_t me;
    std::map<peer_id_t, address_t> routing_table;

    /* `connections` holds open connections to other peers. It has an entry for
    every peer that we are fully and officially connected to, not including us.
    That means it's a subset of the nodes in `routing_table`. */
    struct connection_t {
        streamed_tcp_conn_t *conn;
        mutex_t send_mutex;
    };
    std::map<peer_id_t, connection_t*> connections;

    /* Writes to `everybody` and `connections` are protected by this mutex so
    we never get redundant connections to the same peer. */
    mutex_t new_connection_mutex;

    /* List of everybody watching for connectivity events. `watchers_mutex` is
    so nobody updates `watchers` while we're iterating over it. */
    intrusive_list_t<event_watcher_t> watchers;
    mutex_t watchers_mutex;

    /* Shutdown process stuff */
    cond_t shutdown_cond;
    drain_semaphore_t shutdown_semaphore;
};

} /* namespace connectivity */

#endif /* __RPC_CONNECTIVITY_CONNECTIVITY_HPP__ */
