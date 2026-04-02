import { describe, expect, it } from "vitest";

import {
  detectModelSourceFormat,
  normalizeModelDownloadSourceUrls,
  shouldShowGgufConversionOptions,
} from "./modelLibrary.js";

describe("model library uploader helpers", () => {
  it("detects GGUF and safetensors sources from URLs", () => {
    expect(
      detectModelSourceFormat("https://example.com/models/demo.gguf"),
    ).toBe("gguf");
    expect(
      detectModelSourceFormat("https://example.com/model-00001.safetensors"),
    ).toBe("safetensors");
  });

  it("normalizes source URL textarea input", () => {
    expect(
      normalizeModelDownloadSourceUrls(
        " https://example.com/a.gguf \n\nhttps://example.com/b.gguf ",
      ),
    ).toEqual(["https://example.com/a.gguf", "https://example.com/b.gguf"]);
  });

  it("shows GGUF conversion controls only for safetensors to GGUF jobs", () => {
    expect(shouldShowGgufConversionOptions("safetensors", "gguf")).toBe(true);
    expect(shouldShowGgufConversionOptions("gguf", "gguf")).toBe(false);
    expect(shouldShowGgufConversionOptions("safetensors", "safetensors")).toBe(false);
  });
});
