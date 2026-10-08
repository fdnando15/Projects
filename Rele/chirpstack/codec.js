// ChirpStack -> Device profiles -> (tu perfil) -> Codec -> "JavaScript functions"
// Pega este fichero entero en el cuadro de texto.
//
// Uplink (fPort 10, 3 bytes):  [estado, motivo, segundosRestantesPulso]
// Downlink (fPort 10):         00 OFF | 01 ON | 02 TOGGLE | 03 hi lo PULSO(seg)

function decodeUplink(input) {
  var b = input.bytes;
  if (b.length < 1) {
    return { errors: ["payload vacio"] };
  }

  var reasons = ["heartbeat", "command", "pulse_end", "boot"];

  return {
    data: {
      relay: b[0] === 1 ? "on" : "off",
      relay_on: b[0] === 1,
      reason: b.length > 1 ? (reasons[b[1]] || "unknown") : "unknown",
      pulse_remaining_s: b.length > 2 ? b[2] : 0
    }
  };
}

// Permite encolar el downlink desde la UI de ChirpStack en JSON, en la
// pestana "Queue" del dispositivo, en vez de teclear los bytes en hex:
//   {"relay": "on"}
//   {"relay": "off"}
//   {"relay": "toggle"}
//   {"relay": "pulse", "seconds": 5}
function encodeDownlink(input) {
  var d = input.data || {};
  var cmd = String(d.relay || d.command || "").toLowerCase();

  if (cmd === "on"  || cmd === "1" || d.relay === true)  { return { bytes: [0x01] }; }
  if (cmd === "off" || cmd === "0" || d.relay === false) { return { bytes: [0x00] }; }
  if (cmd === "toggle")                                  { return { bytes: [0x02] }; }
  if (cmd === "pulse") {
    var s = parseInt(d.seconds || 1, 10);
    if (isNaN(s) || s < 1) { s = 1; }
    if (s > 65535) { s = 65535; }
    return { bytes: [0x03, (s >> 8) & 0xFF, s & 0xFF] };
  }
  return { errors: ["relay debe ser on | off | toggle | pulse"] };
}
