"""Gears of War 3 recomp: guest-sample EDRAM layout for the resolution-scaled resolve shaders.

Applies to Xenia's GPU shader sources as of 28b69c2^ (the revision whose resolve and clear shaders
the ReXGlue SDK ships, verified byte-identical with fxc's default flags). Every change is compiled
only into the *_scaled shaders and only takes effect when bit 30 of the resolve EDRAM info is set.

    python apply-guest-samples.py <src/xenia/gpu/shaders>
"""
import sys

d = sys.argv[1]


def edit(name, pairs):
    p = f'{d}/{name}'
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        old = old.replace('\n', nl)
        new = new.replace('\n', nl)
        assert s.count(old) == 1, (name, old[:100])
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)


# ---- edram.xesli -------------------------------------------------------------------------------
EDRAM_FN = '''// Gears of War 3 recomp: XeEdramOffsetInts with the MSAA sample arrangement
// applied to guest pixels rather than to host pixels. With resolution scaling,
// XeEdramOffsetInts shifts the host pixel index by the sample count, so the
// host subpixel bit and the sample bit trade places, and views of the same
// memory with different sample counts (a 1x scene resolved as a 4x surface for
// a 2x2 downsample) no longer line up. Here the sample bits go next to the
// guest pixel and the host subpixel offset is carried along unchanged (the
// scaling part of Xenia 437a7280cf, keeping this layout's sample order). At 1x
// the result is the same as XeEdramOffsetInts. Used by the resolution-scaled
// resolve shaders when the ROV path draws with the same arrangement.
uint XeEdramOffsetIntsGuestSamples(uint2_xe pixel_index, uint base_tiles,
                                   bool wrap, uint pitch_tiles,
                                   uint msaa_samples, bool is_depth,
                                   uint format_ints_log2,
                                   uint pixel_sample_index,
                                   uint2_xe resolution_scale) {
  uint2_xe guest_pixel = pixel_index / resolution_scale;
  uint2_xe subpixel = pixel_index - guest_pixel * resolution_scale;
  uint2_xe guest_sample_index =
      guest_pixel <<
      uint2_xe(greater_than_equal_xe(
          uint_x2_xe(msaa_samples),
          uint2_xe(kXenosMsaaSamples_4X, kXenosMsaaSamples_2X)));
  guest_sample_index +=
      (uint_x2_xe(pixel_sample_index) >> uint2_xe(1u, 0u)) & 1u;
  uint2_xe rt_sample_index = guest_sample_index * resolution_scale + subpixel;
  uint2_xe tile_size_at_32bpp = uint2_xe(80u, 16u) * resolution_scale;
  uint2_xe tile_size_samples =
      tile_size_at_32bpp >> uint2_xe(format_ints_log2, 0u);
  uint2_xe tile_offset_xy = rt_sample_index / tile_size_samples;
  base_tiles += tile_offset_xy.y * pitch_tiles + tile_offset_xy.x;
  rt_sample_index -= tile_offset_xy * tile_size_samples;
  if (is_depth) {
    uint tile_width_half = tile_size_samples.x >> 1u;
    rt_sample_index.x =
        uint(int(rt_sample_index.x) +
             ((rt_sample_index.x >= tile_width_half) ? -int(tile_width_half)
                                                     : int(tile_width_half)));
  }
  uint address =
      base_tiles * (tile_size_at_32bpp.x * tile_size_at_32bpp.y) +
      ((rt_sample_index.y * tile_size_samples.x + rt_sample_index.x) <<
       format_ints_log2);
  if (wrap) {
    address %= tile_size_at_32bpp.x * tile_size_at_32bpp.y * 2048u;
  }
  return address;
}

'''
anchor = '#endif  // XENIA_GPU_SHADERS_EDRAM_XESLI_'
edit('edram.xesli', [(anchor, EDRAM_FN + anchor)])

# ---- resolve.xesli -----------------------------------------------------------------------------
ADDR = 'XeResolveColorCopySourcePixelAddressIntsYHalfPixelOffsetFilling'
PARAM = '''param_uint_vector_buffer_xe(xe_resolve_edram)
          param_next_after_uint_vector_buffer_xe'''
PASS = '''pass_uint_vector_buffer_xe(xe_resolve_edram)
              pass_next_after_uint_vector_buffer_xe'''


def addr(k, indent='            '):
    off = 'source_pixel' if k == 0 else f'source_pixel + uint2_xe({k}u, 0u)'
    return f'''{indent}{ADDR}(
{indent}    resolve_info, {off})'''


def addrs(ks):
    return ',\n'.join(addr(k) for k in ks)


def accumulate(load, names, args):
    """The 1 / 2 / 4 sample accumulation shared by the three color loaders."""
    adds = '\n'.join(f'          {n} += msaa_resolve_{n};' for n in names)
    adds2 = '\n'.join(f'            {n} += msaa_resolve_{n};' for n in names)
    decl = '\n'.join(f'          float4_xe msaa_resolve_{n};' for n in names)
    outs = ', '.join(f'msaa_resolve_{n}' for n in names)

    def call(offset, indent):
        a = ', '.join(x.format(o=offset) for x in args)
        return f'''{indent}{load}(
{indent}    {PASS.replace(chr(10) + '              ', chr(10) + indent + '    ')}
{indent}    {a},
{indent}    resolve_info.edram_format_ints_log2, resolve_info.edram_format,
{indent}    {('resolve_info.dest_swap, ' if 'Red' in load else '')}{outs});'''

    return f'''        float exp_bias = resolve_info.dest_exp_bias_factor;
        dont_flatten_xe
        if (resolve_info.sample_select >= kXenosCopySampleSelect_01) {{
          uint row_offset = XeResolveGuestSamplesRowOffsetInts(resolve_info);
          // TODO(Triang3l): Gamma-correct resolve for 8_8_8_8_GAMMA.
          exp_bias *= 0.5f;
{decl}
{call('row_offset', '          ')}
{adds}
          dont_flatten_xe
          if (resolve_info.sample_select >= kXenosCopySampleSelect_0123) {{
            uint column_offset =
                XeResolveGuestSamplesColumnOffsetInts(resolve_info);
            exp_bias *= 0.5f;
{call('column_offset', '            ')}
{adds2}
{call('(row_offset + column_offset)', '            ')}
{adds2}
          }}
        }}'''


LOADERS = f'''    #ifdef XE_RESOLVE_RESOLUTION_SCALED
      // Gears of War 3 recomp: loaders for edram_guest_samples with MSAA. The
      // host pixels of a multisampled surface are no longer a constant stride
      // apart (the sample bits sit between guest pixels, the host subpixels
      // within them), so every pixel is addressed on its own, and the other
      // samples of a pixel are resolution_scale rows / columns away.

      uint XeResolveGuestSamplesRowOffsetInts(XeResolveInfo resolve_info) {{
        return resolve_info.resolution_scale.y *
               (80u * resolve_info.resolution_scale.x);
      }}

      uint XeResolveGuestSamplesColumnOffsetInts(XeResolveInfo resolve_info) {{
        return resolve_info.resolution_scale.x
               << resolve_info.edram_format_ints_log2;
      }}

      void XeResolveLoad2RGBAUnswappedPixelSamplesFromAddresses(
          {PARAM}
          uint2_xe addresses, uint format_ints_log2, uint format,
          out_param_xe(float4_xe, pixel_0), out_param_xe(float4_xe, pixel_1)) {{
        dont_flatten_xe if (format_ints_log2 != 0u) {{
          uint4_xe packed;
          packed.xy = uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.x);
          packed.zw = uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.y);
          XeResolveUnpack64bpp2Samples(packed, format, pixel_0, pixel_1);
        }} else {{
          uint2_xe packed;
          packed.x = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.x);
          packed.y = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.y);
          XeResolveUnpack32bpp2Samples(packed, format, pixel_0, pixel_1);
        }}
      }}

      void XeResolveLoad4RGBAUnswappedPixelSamplesFromAddresses(
          {PARAM}
          uint4_xe addresses, uint format_ints_log2, uint format,
          out_param_xe(float4_xe, pixel_0), out_param_xe(float4_xe, pixel_1),
          out_param_xe(float4_xe, pixel_2), out_param_xe(float4_xe, pixel_3)) {{
        dont_flatten_xe if (format_ints_log2 != 0u) {{
          uint4_xe packed_01, packed_23;
          packed_01.xy =
              uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.x);
          packed_01.zw =
              uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.y);
          packed_23.xy =
              uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.z);
          packed_23.zw =
              uint_vector_buffer_load2_xe(xe_resolve_edram, addresses.w);
          XeResolveUnpack64bpp4Samples(packed_01, packed_23, format, pixel_0,
                                       pixel_1, pixel_2, pixel_3);
        }} else {{
          uint4_xe packed;
          packed.x = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.x);
          packed.y = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.y);
          packed.z = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.z);
          packed.w = uint_vector_buffer_load1_xe(xe_resolve_edram, addresses.w);
          XeResolveUnpack32bpp4Samples(packed, format, pixel_0, pixel_1,
                                       pixel_2, pixel_3);
        }}
      }}

      void XeResolveLoad8RedPixelSamplesFromAddresses(
          {PARAM}
          uint4_xe addresses_0123, uint4_xe addresses_4567,
          uint format_ints_log2, uint format, bool swap_32bpp,
          out_param_xe(float4_xe, pixels_0123),
          out_param_xe(float4_xe, pixels_4567)) {{
        uint4_xe packed_0123, packed_4567;
        packed_0123.x =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_0123.x);
        packed_0123.y =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_0123.y);
        packed_0123.z =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_0123.z);
        packed_0123.w =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_0123.w);
        packed_4567.x =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_4567.x);
        packed_4567.y =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_4567.y);
        packed_4567.z =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_4567.z);
        packed_4567.w =
            uint_vector_buffer_load1_xe(xe_resolve_edram, addresses_4567.w);
        dont_flatten_xe if (format_ints_log2 != 0u) {{
          XeResolveUnpack64bpp8RedUnswappedSamples(packed_0123, packed_4567,
                                                   format, pixels_0123,
                                                   pixels_4567);
        }} else {{
          XeResolveUnpack32bpp8RedSamples(packed_0123, packed_4567, format,
                                          swap_32bpp, pixels_0123,
                                          pixels_4567);
        }}
      }}

      // source_pixel is the pixel passed to
      // {ADDR} for the first
      // pixel; the others follow it horizontally.
      void XeResolveLoad2RGBAColorsGuestSamples(
          {PARAM}
          XeResolveInfo resolve_info, uint2_xe source_pixel,
          out_param_xe(float4_xe, pixel_0), out_param_xe(float4_xe, pixel_1)) {{
        uint2_xe addresses = uint2_xe(
{addrs(range(2))});
        XeResolveLoad2RGBAUnswappedPixelSamplesFromAddresses(
            {PASS.replace('              ', '            ')}
            addresses, resolve_info.edram_format_ints_log2,
            resolve_info.edram_format, pixel_0, pixel_1);
{accumulate('XeResolveLoad2RGBAUnswappedPixelSamplesFromAddresses', ['pixel_0', 'pixel_1'], ['addresses + {o}'])}
        pixel_0 *= exp_bias;
        pixel_1 *= exp_bias;
        dont_flatten_xe if (resolve_info.dest_swap) {{
          pixel_0 = pixel_0.bgra;
          pixel_1 = pixel_1.bgra;
        }}
      }}

      void XeResolveLoad4RGBAColorsGuestSamples(
          {PARAM}
          XeResolveInfo resolve_info, uint2_xe source_pixel,
          out_param_xe(float4_xe, pixel_0), out_param_xe(float4_xe, pixel_1),
          out_param_xe(float4_xe, pixel_2), out_param_xe(float4_xe, pixel_3)) {{
        uint4_xe addresses = uint4_xe(
{addrs(range(4))});
        XeResolveLoad4RGBAUnswappedPixelSamplesFromAddresses(
            {PASS.replace('              ', '            ')}
            addresses, resolve_info.edram_format_ints_log2,
            resolve_info.edram_format, pixel_0, pixel_1, pixel_2, pixel_3);
{accumulate('XeResolveLoad4RGBAUnswappedPixelSamplesFromAddresses', ['pixel_0', 'pixel_1', 'pixel_2', 'pixel_3'], ['addresses + {o}'])}
        pixel_0 *= exp_bias;
        pixel_1 *= exp_bias;
        pixel_2 *= exp_bias;
        pixel_3 *= exp_bias;
        dont_flatten_xe if (resolve_info.dest_swap) {{
          pixel_0 = pixel_0.bgra;
          pixel_1 = pixel_1.bgra;
          pixel_2 = pixel_2.bgra;
          pixel_3 = pixel_3.bgra;
        }}
      }}

      void XeResolveLoad8RedColorsGuestSamples(
          {PARAM}
          XeResolveInfo resolve_info, uint2_xe source_pixel,
          out_param_xe(float4_xe, pixels_0123),
          out_param_xe(float4_xe, pixels_4567)) {{
        uint4_xe addresses_0123 = uint4_xe(
{addrs(range(4))});
        uint4_xe addresses_4567 = uint4_xe(
{addrs(range(4, 8))});
        if (resolve_info.dest_swap) {{
          // Likely want to load the blue part from the right half for 64bpp.
          addresses_0123 += resolve_info.edram_format_ints_log2;
          addresses_4567 += resolve_info.edram_format_ints_log2;
        }}
        XeResolveLoad8RedPixelSamplesFromAddresses(
            {PASS.replace('              ', '            ')}
            addresses_0123, addresses_4567, resolve_info.edram_format_ints_log2,
            resolve_info.edram_format, resolve_info.dest_swap, pixels_0123,
            pixels_4567);
{accumulate('XeResolveLoad8RedPixelSamplesFromAddresses', ['pixels_0123', 'pixels_4567'], ['addresses_0123 + {o}', 'addresses_4567 + {o}'])}
        pixels_0123 *= exp_bias;
        pixels_4567 *= exp_bias;
      }}
    #endif  // XE_RESOLVE_RESOLUTION_SCALED
'''

end = '  #endif  // XE_RESOLVE_COPY_EDRAM_IS_UINT_VECTOR_BUFFER'
edit('resolve.xesli', [
    ('''  uint2_xe half_pixel_offset_fill_source;
  uint2_xe edram_offset_scaled;
''', '''  uint2_xe half_pixel_offset_fill_source;
  #ifdef XE_RESOLVE_RESOLUTION_SCALED
    // Gears of War 3 recomp: EDRAM samples arranged per guest pixel
    // (XeEdramOffsetIntsGuestSamples), set by the host for the ROV path.
    bool edram_guest_samples;
  #endif
  uint2_xe edram_offset_scaled;
'''),
    ('''    } else {
      resolve_info.half_pixel_offset_fill_source = uint_x2_xe(0u);
    }
  #else''', '''    } else {
      resolve_info.half_pixel_offset_fill_source = uint_x2_xe(0u);
    }
    resolve_info.edram_guest_samples = (edram_info & (1u << 30u)) != 0u;
  #else'''),
    (f'''  uint {ADDR}(
      XeResolveInfo resolve_info, uint2_xe pixel_index) {{
    return XeEdramOffsetInts(''', f'''  uint {ADDR}(
      XeResolveInfo resolve_info, uint2_xe pixel_index) {{
    #ifdef XE_RESOLVE_RESOLUTION_SCALED
      dont_flatten_xe if (resolve_info.edram_guest_samples) {{
        return XeEdramOffsetIntsGuestSamples(
            uint2_xe(pixel_index.x,
                     max(pixel_index.y,
                         resolve_info.half_pixel_offset_fill_source.y)) +
                resolve_info.edram_offset_scaled,
            resolve_info.edram_base_tiles, true, resolve_info.edram_pitch_tiles,
            resolve_info.edram_msaa_samples, false,
            resolve_info.edram_format_ints_log2,
            XeResolveFirstSampleIndex(resolve_info.sample_select),
            resolve_info.resolution_scale);
      }}
    #endif
    return XeEdramOffsetInts('''),
    (end, LOADERS + end),
])

# ---- resolve_full_*: route multisampled guest-sample resolves to the new loaders ----------------
COND = ('resolve_info.edram_guest_samples &&\n'
        '                  resolve_info.edram_msaa_samples != kXenosMsaaSamples_1X')
CALL_PASS = '''pass_uint_vector_buffer_xe(xe_resolve_edram)
      pass_next_after_uint_vector_buffer_xe'''


def route(name, outs, loader, source_pixel_text, decl):
    old_call = f'''  {decl}
  {loader}(
      {CALL_PASS}
      resolve_info,
      {ADDR}(
          resolve_info{source_pixel_text}),
      {outs});
'''
    sp = source_pixel_text.lstrip(',').strip()
    sp = sp.replace('\n', '\n  ')
    new_call = f'''  {decl}
  #ifdef XE_RESOLVE_RESOLUTION_SCALED
  dont_flatten_xe if ({COND}) {{
    {loader}GuestSamples(
        pass_uint_vector_buffer_xe(xe_resolve_edram)
        pass_next_after_uint_vector_buffer_xe
        resolve_info, {sp},
        {outs});
  }} else
  #endif
  {{
  {loader}(
      {CALL_PASS}
      resolve_info,
      {ADDR}(
          resolve_info{source_pixel_text}),
      {outs});
  }}
'''
    edit(name, [(old_call, new_call)])


route('resolve_full_8bpp.xesli', 'pixels_0123, pixels_4567', 'XeResolveLoad8RedColors',
      ', pixel_index', 'float4_xe pixels_0123, pixels_4567;')
for n in ('resolve_full_16bpp.xesli', 'resolve_full_32bpp.xesli', 'resolve_full_64bpp.xesli'):
    route(n, 'pixel_0, pixel_1, pixel_2, pixel_3', 'XeResolveLoad4RGBAColors', ', pixel_index',
          'float4_xe pixel_0, pixel_1, pixel_2, pixel_3;')
route('resolve_full_128bpp.xesli', 'pixel_0, pixel_1', 'XeResolveLoad2RGBAColors',
      ''',
          uint2_xe(max(pixel_index.x,
                       resolve_info.half_pixel_offset_fill_source.x),
                   pixel_index.y)''', 'float4_xe pixel_0, pixel_1;')

# ---- resolve_fast_* -----------------------------------------------------------------------------


def guest_offset(px, msaa, depth, ints, indent):
    i = indent
    return f'''XeEdramOffsetIntsGuestSamples(
{i}    uint2_xe({px},
{i}             max(pixel_index.y,
{i}                 resolve_info.half_pixel_offset_fill_source.y)) +
{i}        resolve_info.edram_offset_scaled,
{i}    resolve_info.edram_base_tiles, true, resolve_info.edram_pitch_tiles,
{i}    {msaa}, {depth}, {ints},
{i}    XeResolveFirstSampleIndex(resolve_info.sample_select),
{i}    resolve_info.resolution_scale)'''


# 1x/2x: consecutive host pixels stay consecutive samples (2x only doubles rows), so only the first
# address changes.
for name, var, shift, depth, ints in (
        ('resolve_fast_32bpp_1x2xmsaa.xesli', 'source_address_int4s', '2u',
         'resolve_info.edram_is_depth', '0u'),
        ('resolve_fast_64bpp_1x2xmsaa.xesli', 'source_address_int4s', '2u', 'false', '1u')):
    old = f'''          resolve_info.edram_msaa_samples, {depth}, {ints},
          XeResolveFirstSampleIndex(resolve_info.sample_select),
          resolve_info.resolution_scale)
      >> {shift};
'''
    new = old + f'''  #ifdef XE_RESOLVE_RESOLUTION_SCALED
    // Gears of War 3 recomp: EDRAM samples arranged per guest pixel.
    dont_flatten_xe if (resolve_info.edram_guest_samples) {{
      {var} =
          {guest_offset('pixel_index.x', 'resolve_info.edram_msaa_samples', depth, ints, '          ')}
          >> {shift};
    }}
  #endif
'''
    edit(name, [(old, new)])

# 4x 32bpp: 8 pixels from a uint4 buffer, each addressed on its own.
lines = []
for k in range(8):
    px = 'pixel_index.x' if k == 0 else f'pixel_index.x + {k}u'
    vec = ('pixels_0123', 'pixels_4567')[k // 4]
    lines.append(f'''      address_ints = {guest_offset(px, 'kXenosMsaaSamples_4X', 'resolve_info.edram_is_depth', '0u', '          ')};
      {vec}.{'xyzw'[k % 4]} =
          array_buffer_load_xe(xe_resolve_edram, address_ints >> 2u)[address_ints & 3u];''')
edit('resolve_fast_32bpp_4xmsaa.xesli', [('''  uint4_xe pixels_0123, pixels_4567;
  dont_flatten_xe
  if (resolve_info.sample_select != kXenosCopySampleSelect_2 &&''', '''  uint4_xe pixels_0123, pixels_4567;
  #ifdef XE_RESOLVE_RESOLUTION_SCALED
    // Gears of War 3 recomp: with EDRAM samples arranged per guest pixel, host
    // pixels are not a constant stride apart, so each is loaded on its own.
    dont_flatten_xe if (resolve_info.edram_guest_samples) {
      uint address_ints;
''' + '\n'.join(lines) + '''
    } else
  #endif
  dont_flatten_xe
  if (resolve_info.sample_select != kXenosCopySampleSelect_2 &&''')])

# 4x 64bpp: 4 pixels from a uint2 buffer.
lines = []
for k in range(4):
    px = 'pixel_index.x' if k == 0 else f'pixel_index.x + {k}u'
    vec = ('pixels_01', 'pixels_23')[k // 2]
    lines.append(f'''      {vec}.{('xy', 'zw')[k % 2]} = array_buffer_load_xe(
          xe_resolve_edram,
          {guest_offset(px, 'kXenosMsaaSamples_4X', 'false', '1u', '          ')}
              >> 1u);''')
edit('resolve_fast_64bpp_4xmsaa.xesli', [('''  uint4_xe pixels_01, pixels_23;
  pixels_01.xy = array_buffer_load_xe(xe_resolve_edram, source_address_int2s);
  pixels_01.zw =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 2u);
  pixels_23.xy =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 4u);
  pixels_23.zw =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 6u);
''', '''  uint4_xe pixels_01, pixels_23;
  #ifdef XE_RESOLVE_RESOLUTION_SCALED
    // Gears of War 3 recomp: with EDRAM samples arranged per guest pixel, host
    // pixels are not a constant stride apart, so each is loaded on its own.
    dont_flatten_xe if (resolve_info.edram_guest_samples) {
''' + '\n'.join(lines) + '''
    } else
  #endif
  {
  pixels_01.xy = array_buffer_load_xe(xe_resolve_edram, source_address_int2s);
  pixels_01.zw =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 2u);
  pixels_23.xy =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 4u);
  pixels_23.zw =
      array_buffer_load_xe(xe_resolve_edram, source_address_int2s + 6u);
  }
''')])
print('ok')
