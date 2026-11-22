# Tests

The port has no unit-test framework. Its checks are tools that write a deterministic text file.
CI runs each tool twice and requires the two files to be identical. On a pull request, it then
compares the file with the one the PR's base commit produces. A change that keeps retail
behaviour must leave the file unchanged. A change that alters it on purpose carries the tool's
"expected change" label: the difference is then reported but does not fail the job.

| Tool | Checks | Label |
|------|--------|-------|
| `FLRegress.exe` (`src/FLRegress/`) | Game state after the server starts on the retail data | `regress: expected change` |
| `RpcCodecTest.exe` (`src/RpcCodecTest/`) | The network packet codecs: wire bytes and result codes | `codecs: expected change` |

The `x64 server` job runs RpcCodecTest from the port-only x64 build and requires its output to
equal the Win32 output ([x64.md](x64.md)).

Layout checks (`FL_LAYOUT_ASSERT`: struct sizes and field offsets against retail) and the FLDedicated
start-and-stop job ([dedicated_server.md](dedicated_server.md)) complete the checks.

Some pure functions are checked at compile time with `static_assert`, in the header that defines
them, so every module that includes the header runs the checks when it builds: `MsvcrtAtoi`
(`src/include/msvcrt_compat.h`, against measurements of msvcrt.dll) and RP8's ini number parser
and FVF stride (`src/include/render/rp8_semantics.h`, shared by RP8, RP9 and RPGPU). A failing
case is a build error.

## RpcCodecTest

`RpcCodecTest.exe` compiles the packet codecs of `RemoteClient.dll`, the server's side of the
network: `src/RemoteClient/rpc_client_packets.cpp` and `src/RemoteCommon/`. It uses
RemoteClient's own defines, so the codecs compile as they do in the DLL. It registers every packet
type the way RemoteClient's `Startup` does (COMMON, SERVER, then CLIENT). No retail module
changes: the only edit outside the test is an inline read-only accessor,
`FPacketSerializer::GetTypes`.

For each packet type it records:

- **fresh**: a newly constructed packet, encoded as is.
- **Fixed inputs** (`empty`, `zeros`, `small`, `ascending`, `ones`, four seeded pseudo-random
  buffers): Decode, then Encode the decoded packet. The line holds Decode's result and the bytes
  it consumed, and Encode's result, length and bytes. It also holds two flags:
  - `rt`: whether the encoded payload equals the bytes Decode consumed.
  - `stable`: whether decoding and re-encoding the output gives the same bytes again.
- **prefixes**: Decode's result on every prefix (0..48 bytes) of the `small` input. This shows
  where each codec stops and which error it reports for a short packet.

The `small` input repeats the little-endian DWORDs 1..5, so count and length fields read small
values and variable-length bodies decode. The file is written twice: once as a multiplayer server
and once in single-player mode, since FIREWEAPON and PLAYER_TRADE depend on those flags.

`rt=diff` and `stable=no` are not failures. Retail has codecs that do not round-trip (see
[re/server.md](re/server.md)); the file records today's behaviour, and the base comparison
catches any change to it. The flags help when reviewing a codec. The test is mainly for the x64
port: an x64 build must produce the same file as the Win32 build.

**Determinism.** Packet constructors leave some fields unset, as retail does, and a failed
Decode leaves others unset. The test therefore replaces `operator new` with a zero-filling
allocator that refuses requests above 64 MiB. Packets are never destroyed, because a destructor
would free pointers that a failed Decode never set. A codec that faults is recorded as
`exception:<code>` and the test carries on.

**No game data.** The test loads no game data. Codecs that look up archetypes (CREATESHIP,
CREATESOLAR and a few others) therefore see empty databases, as they would before the data is
loaded: the lookups return 0, and the misses they report through `FDUMP` are only counted
(`engine_messages` in the summary). Running the test on the retail data is a possible next step.

**Run-dependent output.** Two retail codecs would make the file differ between runs:

- `TRADERESPONSE` (0x30027) encodes the value of its `Data` pointer, not the bytes it points
  to. The test overwrites those four bytes (offset 6) with `ee ee ee ee`. On x64 this field
  would carry only the low half of the pointer.
- `PLAYERLIST` (0x20052) decodes the name with `malloc` and no terminator, and its encoder takes
  `wcslen` of it. When an input's name has no terminator, the decode is recorded but the encode
  is skipped.

**Overflows fixed in the port.** Retail `RemoteClient.dll` corrupts the heap on some of these
inputs: `SET_WEAPON_GROUP` (0x10011) with size `0xffffffff`, `SENDCOMM` (0x2004f) with more
than 8 costume accessories, and `SETREPUTATION` (0x20029) in single-player mode with a negative
display-name length, plus `CREATESHIP` (0x20004) with a long display name, which no current
input reaches. The test used to skip them. The port's codecs now reject them with `0x17`
([re/deviations.md](re/deviations.md#remoteclient-hostile-packets-refused-remote-packet-hardening-2026-11-22)),
so they are recorded like any other failed decode.

Run it from `bin\`:

```
RpcCodecTest.exe [--out rpc_codecs.txt]
```

The output holds no game data, so CI uploads it as the `rpc-codecs` artifact.

Not covered yet: `RemoteServer.dll`'s copy of the codecs (the client side), and the codecs'
callers (packet dispatch and the DirectPlay transport).

## Debug builds

Pull requests build Release only. The `Debug builds` workflow (`.github/workflows/debug-builds.yml`)
builds Debug|Win32, DebugASan|Win32 and Debug|x64 every Monday and when started by hand
(Actions → Debug builds → Run workflow), so a configuration that stops compiling is caught within
a week. It only builds; it runs no tests.
