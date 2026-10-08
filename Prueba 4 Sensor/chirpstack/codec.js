// ChirpStack -> Device profile -> Codec -> JavaScript functions
// Payload: [0..1] int16 temperatura x100, [2..3] uint16 humedad x100

function decodeUplink(input) {
  var b = input.bytes;
  if (b.length < 4) {
    return { errors: ["payload demasiado corto"] };
  }

  var t = (b[0] << 8) | b[1];
  if (t & 0x8000) { t -= 0x10000; }          // signo (temperaturas negativas)
  var h = (b[2] << 8) | b[3];

  return {
    data: {
      temperature: t / 100.0,   // grados Celsius
      humidity: h / 100.0       // % HR
    }
  };
}

function encodeDownlink(input) {
  return { bytes: [] };
}
