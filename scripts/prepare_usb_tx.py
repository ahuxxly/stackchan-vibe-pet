"""One hash-pinned HWCDC pending-TX service repair; offline, no installs or USB IO."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
REL = Path('.toolchain/packages/framework-arduinoespressif32/cores/esp32/HWCDC.cpp')
ORIGINAL_SHA256 = 'd0a8ca606c2729c8522a041113285dbf27033c22a5a6af8649a7305ffe84c449'
ARCHIVE = Path('vendor/arduino-hwcdc-original/HWCDC.cpp')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patched(raw):
    if digest(raw) != ORIGINAL_SHA256:
        raise ValueError('Unexpected pinned Arduino HWCDC original')
    text = raw.decode()
    before = '''    if (connected) {
        running = false;
        return true;
    }
'''
    after = '''    // StackChan: RX can mark connected=true without restarting queued TX.
    // Service pending bytes independently of the next whole-packet admission.
    // No wait, queue receive/discard, extra payload, or claimed host delivery.
    // Only one foreground caller holds tx_lock; the existing ISR owns FIFO data.
    if (tx_ring_buf != NULL && tx_lock != NULL && xSemaphoreTake(tx_lock, 0) == pdPASS) {
        static uint32_t last_tx_service_ms = 0;
        const uint32_t now = millis();
        if (uint32_t(now - last_tx_service_ms) >= 20) {
            last_tx_service_ms = now;
            UBaseType_t waiting = 0;
            vRingbufferGetInfo(tx_ring_buf, NULL, NULL, NULL, NULL, &waiting);
            if (waiting && usb_serial_jtag_ll_txfifo_writable() &&
                !(usb_serial_jtag_ll_get_intr_ena_status() & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY)) {
                // Publish an available FIFO and re-arm its existing consumer.
                // If ISR state changes concurrently, the next service retries;
                // neither queued data nor connected is modified here.
                usb_serial_jtag_ll_txfifo_flush();
                usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
            }
        }
        xSemaphoreGive(tx_lock);
    }

    if (connected) {
        running = false;
        return true;
    }
'''
    if text.count(before) != 1:
        raise ValueError('Pinned HWCDC connection context differs')
    return text.replace(before, after).encode()


def original(root=ROOT):
    saved = root / ARCHIVE
    raw = saved.read_bytes() if saved.is_file() else (root / REL).read_bytes()
    if digest(raw) != ORIGINAL_SHA256:
        raise ValueError('Unreviewed HWCDC original')
    return raw


def prepare(root=ROOT, check=False):
    raw = original(root)
    output = patched(raw)
    target, saved = root / REL, root / ARCHIVE
    current = target.read_bytes()
    if current not in (raw, output):
        raise ValueError('Unreviewed local HWCDC changes; will not overwrite')
    if check and current != output:
        raise ValueError('HWCDC TX service patch missing')
    files = [{'file': str(REL), 'original_sha256': ORIGINAL_SHA256, 'sha256': digest(output)}]
    if not check:
        saved.parent.mkdir(parents=True, exist_ok=True)
        if not saved.exists():
            saved.write_bytes(raw)
        if current != output:
            target.write_bytes(output)
        (root / 'evidence/usb-tx-driver.json').write_text(json.dumps({'files': files}, indent=2) + '\n')
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    print(json.dumps(prepare(check=parser.parse_args().check), indent=2))
