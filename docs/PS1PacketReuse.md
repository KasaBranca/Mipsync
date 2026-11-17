# Reusing PS1 entity packets

The normal renderer regenerates entity commands on every draw. A custom
project renderer can retain unchanged commands by defining
`PS1_RENDER_ENTITY_PACKET_HOOKS=1` when compiling the runtime and providing
the two callbacks declared in `render.h`. The default is disabled: ordinary
projects need no callbacks and retain the existing drawing path.

`ps1_render_entity_packet_replay` runs at the entity's normal submission
position. Return a negative value for a cache miss and leave the ordering
table and packet cursor unchanged. On a hit, append the complete fragment,
advance the cursor, update the relevant ordering-table buckets, and return
the number of primitives submitted. Zero is a valid cached empty draw.

On a miss, the normal renderer draws the entity, then calls
`ps1_render_entity_packet_capture` with the exact emitted packet range.
Capture can decline to retain a draw. It must not alter emitted commands or
their submission order, and must not retain pointers beyond the source
buffer's lifetime.

The application owns its cache and invalidation rules. Include every input
that can affect emitted commands: camera and projection, transforms, mesh
and animation, materials and texture residency, lighting, fog and viewmodel
state. A matching entity pointer alone does not prove a cache hit. Do not
overwrite packet memory while the GPU is still consuming it.

## Packet fragments

`packet_fragment.h` provides helpers without choosing a cache policy or
allocating storage:

- `ps1_packet_fragment_capture` discovers the bucket-local chains owned by
  one contiguous packet range. Supply a bitset of at least
  `(words + 31) / 32` words and enough bucket entries. It rejects truncated
  packets, payload links, cycles, shared ownership and unreachable headers.
  Check its return value before using the entries.
- `ps1_packet_fragment_copy` copies a validated fragment, relocates internal
  DMA links, and attaches its tails to the current ordering-table heads.
  Unowned buckets stay unchanged. The caller must check destination capacity
  and aligned 24-bit DMA ranges. Source packets and captured entries must
  remain valid; this hot-path function does not revalidate them.

Separate buffers and downward overlapping copies are supported. Upward
overlap is not. When CPU storage and DMA base are both unchanged, the helper
retains payload and internal links and only rejoins the current frame's
bucket predecessors. Equal DMA bases with different CPU buffers still copy;
changing the DMA base still relocates links.

The hooks do not reserve packet memory or increase the renderer's cache
capacities. Measure the cache's memory use and hit rate for the project;
dynamic entities can simply keep using normal drawing.
