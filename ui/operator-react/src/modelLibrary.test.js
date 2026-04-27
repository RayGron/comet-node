import { describe, expect, it } from "vitest";

import {
  defaultModelDownloadTarget,
  detectModelSourceFormat,
  eligibleModelStorageNodes,
  modelLibraryJobProgress,
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

  it("treats safetensors plus HF metadata files as safetensors source", () => {
    expect(
      detectModelSourceFormat([
        "https://huggingface.co/example/model/config.json",
        "https://huggingface.co/example/model/chat_template.jinja",
        "https://huggingface.co/example/model/tokenizer.json",
        "https://huggingface.co/example/model/model-00001-of-00002.safetensors",
        "https://huggingface.co/example/model/model-00002-of-00002.safetensors",
      ]),
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

  it("selects the only connected storage-capable node as the default target", () => {
    expect(
      defaultModelDownloadTarget({
        roots: ["/models"],
        nodes: [
          {
            node_name: "local-hostd",
            role_eligible: true,
            registration_state: "registered",
            session_state: "connected",
            storage_root: "/srv/naim",
          },
        ],
      }),
    ).toEqual({ targetNodeName: "local-hostd", targetRoot: "" });
  });

  it("falls back to a manual root when storage node selection is ambiguous", () => {
    const nodes = [
      {
        node_name: "node-a",
        role_eligible: true,
        registration_state: "registered",
        session_state: "connected",
        storage_root: "/srv/a",
      },
      {
        node_name: "node-b",
        role_eligible: true,
        registration_state: "registered",
        session_state: "connected",
        storage_root: "/srv/b",
      },
    ];
    expect(eligibleModelStorageNodes(nodes)).toHaveLength(2);
    expect(defaultModelDownloadTarget({ roots: ["/models"], nodes })).toEqual({
      targetNodeName: "",
      targetRoot: "/models",
    });
  });

  it("calculates progress for acquiring model download jobs", () => {
    expect(
      modelLibraryJobProgress({
        status: "running",
        phase: "acquiring-model",
        bytes_done: 25,
        bytes_total: 100,
      }),
    ).toBe(25);
  });

  it("does not report progress without a valid byte total", () => {
    expect(
      modelLibraryJobProgress({
        status: "running",
        phase: "acquiring-model",
        bytes_done: 25,
        bytes_total: null,
      }),
    ).toBe(null);
  });
});
