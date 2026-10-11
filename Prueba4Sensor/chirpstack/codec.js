// Codec ChirpStack: [0..1] temperatura int16 x100, [2..3] humedad uint16 x100

function decodeUplink(input) {
  var b = input.bytes;
  if (b.length < 4) {
    return { errors: ["payload demasiado corto"] };
  }

  var t = (b[0] << 8) | b[1];
  if (t & 0x8000) { t -= 0x10000; }
  var h = (b[2] << 8) | b[3];

  return {
    data: {
      temperature: t / 100.0,
      humidity: h / 100.0
    }
  };
}

function encodeDownlink(input) {
  return { bytes: [] };
}
