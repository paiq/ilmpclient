ILMP specification 1.0-draft
============================

Summary
-------

The Implicit Link Messaging Protocol (ILMP) is a two-way rpc protocol, in which clients can register callback references with a channel subscription model on an Implicit Link Comet Server (ILCS). It is a simplified implementation of the proprietary protocol used between ILCS and (javascript based) clients running on Implicit Link comet websites, such as tjetter.nl and paiq.nl.

Message exchange and callbacks
------------------------------

ILMP is based on a long-living TCP session between ILCS and a client, over which messages can be sent both ways. In this document, messages *to* and *from* ILCS are referred to as *outgoing messages* and *incoming messages*, respectively. Message exchange occurs completely asynchronous, and is not subject to a any request-response exchange pattern.

### Initialization ###

An ILMP client initiates a TCP connection to an ILCS server. When connecting succeeds, the client should send the following initialization sequence:

	"GET /ilcs? ILMP/1.0\n\n"

Note that although this sequence may resemble a HTTP request, ILMP is not to be regarded as a protocol that is partly based around the HTTP specification. This initial sequence was chosen only because it allows a more uniform ILCS implementations that serves both HTTP and ILMP clients.

### Regular messages ###

#### Outgoing messages ####

	outgoing_message := [pageview_id] \x002 "M" ([site] "|")? [rpc] (\x003 [param])* \x001

	param := "p" [plain_text]
	       | "j" [json]
	       | "c" [callback_id]

Regular outgoing messages consist of an optional site specification, an rpc identifier and zero or more parameters. These parameters may contain one or more special references to locally coupled callback identifiers through the field *callback_id*.

#### Incoming messages ####

	incoming_message := [sequence_id] \x002 [pageview_id] \x002 [callback_id] \x002 [callback_ref_instr] \x002 ((param \x004?)* \x003?)* \x001
	
	callback_ref_instr := "" | "+" | "-" | ([0-9]+)</pre>

Regular incoming messages contain a callback identifier issued by the client in an outgoing message. The client uses it to invoke the callback with the parameters supplied by in the message. Clients should keep track of all local callback identifiers; ILCS keeps track of all callback identifiers of all clients.

#### Invalidation of callbacks ####

Local callback identifiers should remain valid, even after the callback is invoked. ILCS instructions allow a client to determine when a callback is not necessary anymore and may be cleaned up. These instructions are encapsulated in *callback_ref_instr* and require clients to associate an *ilcs_refcount* integer with each local callback. An incoming message may choose to increment, decrement or set an absolute value of the *ilcs_refcount* associated with the callback. When *ilcs_refcount* is 0 or less, the callback will never to be invoked again and associated resources might be cleaned up. New callback identifiers should start with an associated *ilcs_refcount* of 1.

### Session multiplexing ###

Both incoming and outgoing messages contain a *pageview_id* that identifies a session. Pageviews (sessions) are created implicitly when a client issues a command with an unknown *pageview_id*. Pageview identifiers are bound to a single ILMP connection and can effectively be used to multiplex multiple sessions over a single connection. All associated pageviews are destroyed once the connection is terminated (in other words, pageview identifiers can not be used to restore a session). Callback id's are unique per pageview, not necessarily per connection.

### Incoming message sequence ###

The *sequence_id* field of incoming messages consists of an incrementing (and zero-wrapping) 16-bit unsigned integer, transferred as string. Clients should validate this sequence. The first incoming message has a *sequence_id* of 1.

### Special messages ###

#### Ping / pong ####

In order to detect connection drops more accurately, a client may regularly issue ping messages.

	ping_message := "P" \x001

ILCS should respond with a pong message in a reasonable, but unspecified, time:

	pong_message := [sequence_id] \x002 "P" \x001

The sequence_id increments similarly and transparently to regular incoming messages. When a client does not implement sending of ping messages, ILCS will never issue pong messages.

#### Protocol version mismatch ####

ILCS may, at any time, send a message indicating that the client's protocol version is outdated. This may be followed by a disconnect.

	protocol_version_outdated_message := [sequence_id] \x002 "U" \x001

Application considerations
--------------------------

### Reconnect delay ###

To prevent server congestion, clients should implement a strategy to delay reconnecting once a connection fails or is unexpectedly dropped. Clients may choose the strategy to use, but a reconnect delay in accordance with the following formula is suggested:

	delay_in_seconds = min(60 * 10, 3 * ( 2 ^ (subsequent_connect_failures) ) )

License
-------
This specification is released under the GNU Free Document License.