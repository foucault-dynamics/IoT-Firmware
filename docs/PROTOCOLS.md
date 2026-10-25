# Link protocols

This page explains how a reading gets from a meter node to the MQTT broker, and
what each hop does to make sure it is not lost. It covers the discovery
handshake, the acknowledgements on each link, the sequence numbers that make
retries safe, and what happens when something goes wrong.

```text
  meter node ──ESP-NOW──► substation ──LoRa──► gateway ──MQTT──► broker
              (hop 1)                 (hop 2)           (hop 3)
```

Each hop is a separate, short protocol. There is no end to end ACK. A node
learns only that the substation kept its reading, and the substation learns only
that the gateway received it.

The wire formats are all in `lib/interfaces/shared/shared_payload.h`. Every board
must be built from the same copy of that header, because receivers tell frames
apart by size and drop anything else.

## The packets

| Struct | Size | Carries | Used on |
|---|---|---|---|
| `Payload` | 34 bytes | UID, `seq`, import, export, voltage, community, unit, node type | ESP-NOW and LoRa |
| `AckPayload` | 20 bytes | UID and `seq` of the reading being acknowledged | ESP-NOW and LoRa |
| `LinkFrame` | 23 bytes | `"KZ"` magic, type (Beacon, Join, JoinAck), epoch, UID | ESP-NOW only |

Sizes differ on purpose. A receiver can tell the three apart from the length
alone, and `LinkFrame` carries a magic as well so stray ESP-NOW traffic from
other devices is ignored.

An ACK has no checksum or signature of its own. It is matched purely on the UID
and `seq` it echoes, so the sender can tell which reading it answers.

## Sequence numbers

Every reading carries a per node `seq` from `seqNext()`. It is kept in NVS in a
namespace the `clear` command does not wipe, so it keeps counting across
reboots. Its job is to let the receiver recognise a retry of a reading it
already has. Without it, a lost ACK would make the sender resend and the
receiver would store the same reading twice.

## Hop 1: meter node to substation (ESP-NOW)

ESP-NOW has a delivery confirmation built into the radio stack, but it only says
a frame reached the other radio. It does not say the substation kept the
reading. So the project adds its own ACK on top, sent only once the reading is
inside the substation's buffer.

### Discovery and join

A meter node does not know the substation's MAC, so it finds it.

```text
substation                          meter node
    │── Beacon (broadcast, 1/s) ───────►│  carries a random epoch
    │                                   │  join() waits up to 3 s for one
    │◄─────── Join (UID, epoch) ────────│  up to 3 tries
    │── JoinAck (echoes UID, epoch) ───►│  node is now "joined"
```

- The substation broadcasts a `Beacon` every second. It carries an `epoch`, a
  random number picked at boot.
- The node takes the first beacon it hears and sends a `Join` to that sender
  with its UID and the epoch it heard.
- The substation adds the node to its joined table and answers with a `JoinAck`.
  A `Join` carrying an old epoch is ignored. If the table is full (8 nodes, the
  same as the reading buffer's slots) the node gets no `JoinAck` and keeps
  looking.
- A lost `JoinAck` is harmless. The node sends the `Join` again and is
  re-acknowledged.

The epoch is how a reboot is noticed. When the substation restarts it forgets
every joined node. Its next beacons carry a new epoch, the node sees the change
and rejoins on its next send, instead of sending readings that would be dropped.

### Sending a reading

```text
meter node                          substation
    │── Payload (uid, seq, ...) ───────►│  must be from a joined MAC
    │                                   │  seq == last ACKed seq? → just re-ACK
    │                                   │  otherwise push into reading buffer
    │◄──── AckPayload (uid, seq) ───────│  only after the push succeeded
```

`SubstationLink::send()` does this, and waits up to 1 s (`DELIVERY_ACK_WAIT_MS`)
for the matching `AckPayload`. The wait has to outlast one pass of the
substation's loop, which can include a LoRa transmit.

The substation answers in three ways:

| Situation | Substation does | Node sees |
|---|---|---|
| Joined node, new `seq` | Buffers it, sends the ACK | Success |
| Joined node, same `seq` as the last one ACKed | Does not buffer again, sends the ACK again | Success |
| Unjoined MAC, or buffer refused the reading | Drops it, sends nothing | Timeout |

No ACK is deliberate for the last row. An unjoined node learns it is not joined
by timing out, and then joins again.

If the send fails or no ACK arrives, the node forgets the substation, rejoins
straight away and resends the same reading. That is `SEND_ATTEMPTS = 2` tries in
all. If both fail the send returns failure and the next reading starts again
with discovery.

The duplicate check compares only against the last `seq` ACKed for that node,
using equality rather than "less than or equal". That is enough, since a node
only ever has one reading in flight, and it means a node whose counter was wiped
is not locked out.

## Hop 2: substation to gateway (LoRa)

LoRa is a single shared broadcast channel with no delivery confirmation, so the
whole ACK and retry scheme is ours, in `LoRaLink`.

```text
substation                          gateway
    │── Payload ───────────────────────►│  receivePacket() checks the size
    │                                   │  waits 10 ms so the sender can listen
    │◄──── AckPayload (uid, seq) ───────│
```

### Retries

A send is a small state machine, stepped once per `loop()` pass so the
substation can keep beaconing and answering nodes while it waits.

```text
Transmit ──► AwaitAck ──ACK matches──► Acked
                │
              timeout
                ▼
             Backoff (500 ms) ──retries left──► Transmit
                │
            none left
                ▼
              Failed
```

- Each attempt transmits, switches to receive and listens for an `AckPayload`
  with the same UID and `seq` for `lora_ack_ms` (default 1500 ms).
- It makes up to `lora_retries` attempts (default 3), with 500 ms between them.
- Packets of any other size heard while waiting are discarded. An ACK for a
  different reading, such as one meant for another relaying substation, is
  ignored because its UID and `seq` do not match.
- Only the call that transmits blocks, for the packet's airtime (about 0.4 s for
  a `Payload` at SF10 and 125 kHz).

### Store and forward

The substation never relays straight from the radio. A reading from a node goes
into the **reading buffer**, a ring per node keyed by UID. Relaying is
peek, send, then pop:

1. `readingBufferPeek()` copies out the oldest reading of the next node in turn,
   without removing it. It rotates across nodes so a busy one cannot starve the
   rest.
2. `LoRaLink` sends it and waits for the gateway's ACK.
3. On `Acked`, `readingBufferPop()` removes it.
4. On `Failed`, the reading stays in the buffer and the substation waits
   `RETRY_BACKOFF_MS` (30 s) before trying again. New readings keep being
   buffered meanwhile.

So a reading only leaves the substation once the gateway has confirmed it. If
the gateway is off for an hour, the readings wait. If a node's ring fills up, the
oldest reading is overwritten.

## Hop 3: gateway to broker (MQTT)

The gateway ACKs on LoRa as soon as it has received a valid `Payload`, then
queues it. A separate loop pulls it off the queue (32 deep), builds the JSON
(`payload_json`) with a timestamp and the packet's RSSI and SNR, and publishes it
over TLS. The reading is removed from the queue only when `publish()` succeeds,
so a broker outage holds readings in the queue rather than losing them.

Note the ordering: the LoRa ACK means "the gateway has it", not "it reached the
broker". The gateway timestamps a reading when it publishes it, not when the
meter read it.

## What can go wrong

| Failure | What happens |
|---|---|
| Node's `Payload` lost on ESP-NOW | No ACK, the node rejoins and resends once. |
| Substation's ACK lost on ESP-NOW | The node resends. The substation sees the same `seq`, does not buffer it twice, and ACKs again. |
| Substation reboots | New epoch in the beacons. The node forgets it, rejoins on the next send. Readings that were only in the old RAM buffer are lost. |
| Substation's `Payload` lost on LoRa | No ACK, it retries up to 3 times, then keeps the reading and tries again after 30 s. |
| Gateway's ACK lost on LoRa | The substation retries. The gateway receives the same reading again and publishes it again, since it has no duplicate check. |
| Gateway cannot reach the broker | Readings wait in the gateway queue. LoRa receiving stops while the queue is full, and while the gateway is blocked reconnecting to MQTT. |

The last two rows are the known weak spots. Duplicates are possible at the broker
because only the node to substation hop deduplicates, and nothing is encrypted or
authenticated on the radio links yet. Both are listed in
[Future developments](FUTURE_DEVELOPMENTS.md).

## Where the code is

| Piece | Location |
|---|---|
| Packet structs, `LinkFrame` helpers | `lib/interfaces/shared/shared_payload.h` |
| Join and delivery ACK on the node | `lib/links/substation_link/` |
| Beacons, joined table, buffering and relay | `src/nodes/substation.cpp` |
| LoRa ACK and retry | `lib/links/lora_link/` |
| Store and forward buffer | `lib/buffering/reading_buffer/` |
| Persistent sequence numbers | `lib/buffering/seq_counter/` |
| LoRa receive, queue and MQTT publish | `src/nodes/gateway.cpp` |
