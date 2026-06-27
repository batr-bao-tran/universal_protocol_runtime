/** High-level codec facade used by generated protocol modules. */

import * as codec from "./codec.js";
import { DecodeError, EncodeError } from "./errors.js";
import type { Layout, Protocol } from "./metadata.js";

type Values = Record<string, unknown>;

export class Codec {
  readonly protocol: Protocol;
  private readonly messagesByName = new Map<string, Layout>();
  private readonly structsByName = new Map<string, Layout>();

  constructor(protocol: Protocol) {
    this.protocol = protocol;
    for (const message of protocol.messages) {
      this.messagesByName.set(message.name, message);
    }
    for (const struct of protocol.structs) {
      this.structsByName.set(struct.name, struct);
    }
  }

  private layout(name: string, forEncode = false): Layout {
    const layout = this.messagesByName.get(name) ?? this.structsByName.get(name);
    if (layout === undefined) {
      if (forEncode) {
        throw new EncodeError("message not found", name);
      }
      throw new DecodeError("message_not_found", name, 0);
    }
    return layout;
  }

  /**
   * Encodes a value mapping into a frame for the named message/struct.
   *
   * Accepts a plain object so generated typed interfaces (which carry no index
   * signature) can be passed directly.
   */
  encode(name: string, values: Values | object): Uint8Array {
    return codec.encode(this.protocol, this.layout(name, true), values as Values);
  }

  /** Decodes a single frame into a value mapping. */
  decode(name: string, frame: Uint8Array): Values {
    return codec.decode(this.protocol, this.layout(name), frame);
  }

  /** Decodes a packed sequence of records into an array of mappings. */
  decodeSequence(name: string, frame: Uint8Array): Values[] {
    return codec.decodeSequence(this.protocol, this.layout(name), frame);
  }
}
