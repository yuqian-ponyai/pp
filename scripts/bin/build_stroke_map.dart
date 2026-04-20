// Build `data/raw/stroke.dict.yaml` from cnchar's simplified stroke-order data.
//
// Upstream:  https://github.com/theajack/cnchar (MIT)
// Pinned at: commit SHA `cncharSha` below.
//
// cnchar stores each character's stroke order as a string over a 27-letter
// alphabet (a..z + extras) that encodes CJK stroke shapes. Predictable Pinyin
// uses a coarser 6-letter alphabet:
//
//     h = 横   (horizontal)
//     s = 竖   (vertical)
//     p = 撇   (left-falling)
//     n = 捺   (right-falling, including left-falling dot 丶)
//     d = 点   (small dot, including 提/挑 rising hooks — hard to distinguish
//              from 点 visually, so folded together for predictability)
//     z = 折   (any hook/turn variant)
//
// This script downloads cnchar's `stroke-order-jian.json` at the pinned SHA,
// verifies its SHA256, collapses each cnchar letter to one of our 6 classes,
// and emits a tab-separated stroke dict in the format the C++ loader in
// `src/stroke_filter.cc` already parses.
//
// Re-running is a no-op once `data/raw/stroke.dict.yaml` exists — delete that
// file to force regeneration.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

// Pinned source.
const String cncharSha = 'd02588ecc61a5ca9d594288e92d1bb6553b415c2';
const String cncharRepo = 'theajack/cnchar';
const String strokeOrderJsonPath =
    'src/cnchar/plugin/order/dict/stroke-order-jian.json';
const String expectedStrokeOrderSha256 =
    '412e3beb587aa5e8b423655226b956eaef0496e8c06e486d7584b2f3e1dbcdcc';

// 27-letter → 6-letter collapse.
//
// Mapping rationale (see doc/stroke-data.md for the full table):
//   j → h      (横)
//   f → s      (竖)
//   s → p      (撇)
//   l → n      (捺 — cnchar's 'l' is the main right-falling stroke; left-falling
//               dot 丶 in cnchar is also encoded as 'l' via the shape table)
//   k, d, i → d (点 — small dot, plus 提/挑 rising hooks which users find hard
//               to distinguish from 点 visually)
//   everything else → z (any 折/hook/turn variant)
const Map<String, String> _collapse = {
  'j': 'h',
  'f': 's',
  's': 'p',
  'l': 'n',
  'k': 'd',
  'd': 'd',
  'i': 'd',
};

String _collapseStroke(String letter) => _collapse[letter] ?? 'z';

String _collapseSequence(String seq) {
  final buf = StringBuffer();
  for (final ch in seq.split('')) {
    buf.write(_collapseStroke(ch));
  }
  return buf.toString();
}

String _sha256Hex(List<int> bytes) {
  // Dart SDK has no built-in SHA256; implement a minimal one rather than
  // pull in `package:crypto`. Keeps this generator dependency-free.
  return _Sha256().update(bytes).digestHex();
}

Future<Uint8List> _download(String url) async {
  final client = HttpClient();
  try {
    final request = await client.getUrl(Uri.parse(url));
    final response = await request.close();
    if (response.statusCode != 200) {
      throw HttpException('GET $url -> ${response.statusCode}');
    }
    final builder = BytesBuilder(copy: false);
    await for (final chunk in response) {
      builder.add(chunk);
    }
    return builder.toBytes();
  } finally {
    client.close(force: true);
  }
}

String _findRepoRoot(String scriptDir) {
  // scriptDir is .../pinyin/scripts/bin (or similar). Walk up to find a dir
  // that contains both `data/` and `scripts/`.
  var dir = Directory(scriptDir).absolute;
  for (var i = 0; i < 6; i++) {
    final data = Directory('${dir.path}/data');
    final scripts = Directory('${dir.path}/scripts');
    if (data.existsSync() && scripts.existsSync()) return dir.path;
    final parent = dir.parent;
    if (parent.path == dir.path) break;
    dir = parent;
  }
  throw StateError('Could not locate repo root from $scriptDir');
}

Future<int> main(List<String> args) async {
  final scriptDir = File(Platform.script.toFilePath()).parent.path;
  final repoRoot = _findRepoRoot(scriptDir);
  final outFile = File('$repoRoot/data/raw/stroke.dict.yaml');
  final cacheDir = Directory('$repoRoot/.cache/cnchar');

  if (outFile.existsSync()) {
    stdout.writeln(
        'stroke.dict.yaml already present at ${outFile.path}, skipping.');
    return 0;
  }

  cacheDir.createSync(recursive: true);
  final cacheFile = File('${cacheDir.path}/stroke-order-jian.json');

  Uint8List bytes;
  if (cacheFile.existsSync()) {
    bytes = cacheFile.readAsBytesSync();
    final sha = _sha256Hex(bytes);
    if (sha != expectedStrokeOrderSha256) {
      stdout.writeln('cached stroke-order-jian.json has wrong sha256 '
          '($sha), re-downloading.');
      cacheFile.deleteSync();
      bytes = Uint8List(0);
    }
  } else {
    bytes = Uint8List(0);
  }
  if (bytes.isEmpty) {
    final url =
        'https://raw.githubusercontent.com/$cncharRepo/$cncharSha/$strokeOrderJsonPath';
    stdout.writeln('downloading $url');
    bytes = await _download(url);
    final sha = _sha256Hex(bytes);
    if (sha != expectedStrokeOrderSha256) {
      stderr.writeln(
          'sha256 mismatch for stroke-order-jian.json: got $sha, '
          'expected $expectedStrokeOrderSha256');
      return 2;
    }
    cacheFile.writeAsBytesSync(bytes);
  }

  final raw = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
  outFile.parent.createSync(recursive: true);

  final sink = outFile.openWrite();
  sink.writeln(
      '# Auto-generated by scripts/bin/build_stroke_map.dart. Do not edit.');
  sink.writeln('#');
  sink.writeln(
      '# Source: https://github.com/$cncharRepo (MIT), commit $cncharSha');
  sink.writeln('#         $strokeOrderJsonPath');
  sink.writeln('#         sha256 $expectedStrokeOrderSha256');
  sink.writeln('#');
  sink.writeln(
      '# Alphabet: h=横  s=竖  p=撇  n=捺  d=点 (incl. 提/挑)  z=折 (any hook/turn).');
  sink.writeln(
      '# cnchar\'s 27-letter alphabet is collapsed to these 6 classes; see');
  sink.writeln('# doc/stroke-data.md for the full mapping.');
  sink.writeln('#');
  sink.writeln('---');
  sink.writeln('name: stroke');
  sink.writeln('version: "1.0"');
  sink.writeln('sort: by_weight');
  sink.writeln('use_preset_vocabulary: true');
  sink.writeln('max_phrase_length: 1');
  sink.writeln('...');
  sink.writeln();

  final keys = raw.keys.toList()..sort();
  var written = 0;
  for (final k in keys) {
    final v = raw[k];
    if (v is! String || v.isEmpty) continue;
    final collapsed = _collapseSequence(v);
    sink.write(k);
    sink.write('\t');
    sink.writeln(collapsed);
    written++;
  }
  await sink.flush();
  await sink.close();
  stdout.writeln(
      'wrote ${outFile.path} ($written entries, 6-letter alphabet)');
  return 0;
}

// ---------------------------------------------------------------------------
// Minimal SHA-256 (FIPS 180-4) — avoids pulling package:crypto.

class _Sha256 {
  static const List<int> _k = <int>[
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  ];

  final List<int> _state = <int>[
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ];
  final BytesBuilder _buffer = BytesBuilder(copy: false);
  int _totalLength = 0;

  _Sha256 update(List<int> bytes) {
    _buffer.add(bytes);
    _totalLength += bytes.length;
    final raw = _buffer.toBytes();
    final fullBlocks = raw.length ~/ 64;
    for (var i = 0; i < fullBlocks; i++) {
      _processBlock(raw, i * 64);
    }
    final leftover = raw.sublist(fullBlocks * 64);
    _buffer.clear();
    _buffer.add(leftover);
    return this;
  }

  List<int> digest() {
    final bitLen = _totalLength * 8;
    final padded = BytesBuilder(copy: false);
    padded.add(_buffer.toBytes());
    padded.addByte(0x80);
    while (padded.length % 64 != 56) {
      padded.addByte(0);
    }
    final lenBytes = ByteData(8)..setUint64(0, bitLen);
    padded.add(lenBytes.buffer.asUint8List());
    final raw = padded.toBytes();
    for (var i = 0; i < raw.length; i += 64) {
      _processBlock(raw, i);
    }
    final out = Uint8List(32);
    final bd = ByteData.view(out.buffer);
    for (var i = 0; i < 8; i++) {
      bd.setUint32(i * 4, _state[i] & 0xffffffff);
    }
    return out;
  }

  String digestHex() {
    final d = digest();
    final sb = StringBuffer();
    for (final b in d) {
      sb.write(b.toRadixString(16).padLeft(2, '0'));
    }
    return sb.toString();
  }

  static int _rotr(int x, int n) =>
      ((x >> n) | (x << (32 - n))) & 0xffffffff;

  void _processBlock(Uint8List data, int offset) {
    final w = List<int>.filled(64, 0);
    final bd = ByteData.view(data.buffer, data.offsetInBytes + offset, 64);
    for (var i = 0; i < 16; i++) {
      w[i] = bd.getUint32(i * 4);
    }
    for (var i = 16; i < 64; i++) {
      final s0 = _rotr(w[i - 15], 7) ^ _rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      final s1 =
          _rotr(w[i - 2], 17) ^ _rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xffffffff;
    }
    var a = _state[0],
        b = _state[1],
        c = _state[2],
        d = _state[3],
        e = _state[4],
        f = _state[5],
        g = _state[6],
        h = _state[7];
    for (var i = 0; i < 64; i++) {
      final s1 = _rotr(e, 6) ^ _rotr(e, 11) ^ _rotr(e, 25);
      final ch = (e & f) ^ ((~e & 0xffffffff) & g);
      final temp1 = (h + s1 + ch + _k[i] + w[i]) & 0xffffffff;
      final s0 = _rotr(a, 2) ^ _rotr(a, 13) ^ _rotr(a, 22);
      final maj = (a & b) ^ (a & c) ^ (b & c);
      final temp2 = (s0 + maj) & 0xffffffff;
      h = g;
      g = f;
      f = e;
      e = (d + temp1) & 0xffffffff;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) & 0xffffffff;
    }
    _state[0] = (_state[0] + a) & 0xffffffff;
    _state[1] = (_state[1] + b) & 0xffffffff;
    _state[2] = (_state[2] + c) & 0xffffffff;
    _state[3] = (_state[3] + d) & 0xffffffff;
    _state[4] = (_state[4] + e) & 0xffffffff;
    _state[5] = (_state[5] + f) & 0xffffffff;
    _state[6] = (_state[6] + g) & 0xffffffff;
    _state[7] = (_state[7] + h) & 0xffffffff;
  }
}
