# Optional extension candidates

These are researched candidates, not installed or advertised as available packs. Existing QR/barcode and OCR extensions already cover recognition. The normal viewer starts neither their engines nor candidate tools below.

| Candidate | Useful action | Integration assessment |
| --- | --- | --- |
| [Oxipng](https://github.com/oxipng/oxipng) | Recompress PNG/APNG files; report original/result sizes | Best first candidate: native command-line program, no interpreter or model. Start with lossless settings, preserve metadata, and keep alpha-value rewriting disabled. APNG optimization has upstream limitations. |
| [Gifsicle](https://www.lcdf.org/gifsicle/) | Optimize GIF animations, extract frames and change looping/timing | Small command-line integration. Animation timing, disposal and transparency need comparison tests. Lossy options remain separate from lossless optimization. |
| [Potrace](https://potrace.sourceforge.net/) | Trace monochrome logos, scans or signatures into SVG | Simple native command-line backend; threshold preview and export UI are still needed. Not a general color-photo vectorizer. |
| [Real-ESRGAN ncnn Vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan) | Local image enlargement/restoration | More work: models, Vulkan/device checks, GPU memory, cancellation and before/after preview. Native image-in/image-out CLI avoids bundling a Python installation. |

“Easy” describes the backend connection. Packaging three platforms, licenses, integrity checks and result validation remain necessary. Exact download/installed sizes should come from the finished packs, not guessed upstream archive sizes.

## Background removal

A separate native C++ worker using [ONNX Runtime's C/C++ API](https://onnxruntime.ai/docs/get-started/with-cpp.html) can run a selected segmentation model without Python or a background server. Engine and model remain optional downloads, loaded only for an explicit job. The worker exits after processing so its model memory is released. Prefer returning an alpha mask: the viewer can apply it to the original pixels, preserving their color profile and bit depth instead of replacing the photograph with an 8-bit worker export. This is an architectural option rather than an existing üxzView feature: model-specific preprocessing, mask alignment, alpha composition, licensing and difficult-edge fixtures require implementation and validation. ONNX Runtime alone does not remove a background.

[withoutBG](https://withoutbg.com/docs) offers an ONNX open-model ecosystem, but its documented Python/Docker integrations would add their own runtime if shipped as-is. A custom native port must reproduce the selected pipeline; it is not equivalent to invoking the existing Python CLI. No cloud upload is needed for a local native design.

For a first native prototype, use [U²-Net / U²-NetP](https://github.com/xuebinqin/U-2-Net): its mask pipeline is relatively small and an [existing ONNX session](https://github.com/danielgatis/rembg/blob/main/rembg/sessions/u2netp.py) documents preprocessing and output normalization. U²-NetP favors a small download and CPU work; fine hair, fur and translucent objects need quality comparisons before choosing it as the recommended model. The upstream 4.7 MB figure describes its PyTorch weights, not a finished üxzView tool download.

[BiRefNet](https://github.com/ZhengPeng7/BiRefNet) offers a stronger quality-oriented candidate, including a lightweight backbone and upstream ONNX exports. Test it on CPU and normal consumer hardware; published GPU results are not viewer performance guarantees. Runtime/operator compatibility and exact model licensing must be pinned with the weights. Do not substitute BRIA RMBG weights and assume the same license: the upstream BiRefNet project explicitly distinguishes those noncommercial weights.

[rembg](https://github.com/danielgatis/rembg) is convenient for prototyping and comparisons, but its CLI requires Python and an inference backend. Packaging that entire runtime is easy to isolate as a separate optional download, but it is not the smallest native solution. withoutBG's SDK and model also have different licensing layers; inspect the exact pinned [model license](https://huggingface.co/withoutbg/withoutbg-openweights-onnx/blob/main/LICENSE), including its backbone, rather than assuming the SDK license covers all weights.

## API providers

An optional service adapter can send the selected image to a background-removal API and receive a transparent PNG, without installing local inference engines or models. [withoutBG's API](https://withoutbg.com/docs) documents a direct HTTPS image-to-image endpoint. A self-hosted compatible server can use the same provider interface. [remove.bg](https://www.remove.bg/api) also documents an HTTP API, but currently announces a move to Leonardo.Ai from December 1, 2026; recheck its migration before implementing a new integration.

Provider selection, endpoint and the user's own API key belong in settings. Store credentials in the OS credential store, not a portable INI file or catalog. Upload only after an explicit invocation, clearly show the remote provider, support cancellation/timeouts and bounded responses, and keep API errors or credit exhaustion separate from local tool errors. Applying a result must use preview/Undo, and must be rejected if the active document changed during processing. These are implementation requirements, not currently available features. No provider accounts, API uploads or paid calls have been made.

## Hosting downloads on GitHub

Keep tool source/build recipes, the source lock, licenses and small catalog metadata in the repository. Publish `.ixtool` binaries and model packs as **release assets**. GitHub [recommends releases for distributing large binaries](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github); its [release documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases) currently limits each asset to under 2 GiB. The viewer's own per-pack limit remains 512 MiB.

The existing Windows release workflow already attaches all seven `.ixtool` files, `uxzView-tools.json` and `uxzView-tools-SHA256SUMS.txt`, alongside the Windows/source downloads. No additional repository folder containing compiled tools is needed.

1. Build and verify the release using `scripts/prepare_release.ps1`.
2. Upload `dist/tools/*.ixtool`, `dist/tools/uxzView-tools.json` and `dist/tools/uxzView-tools-SHA256SUMS.txt` as assets of the **same release**. Keep license/source downloads available alongside the binaries.
3. For example, the versioned catalog address is `https://github.com/OWNER/REPO/releases/download/v0.4.4/uxzView-tools.json` (replace owner/repo/tag). Use a published release reachable by ordinary users; draft assets are not public downloads.
4. Enter that address in the extension manager and load the catalog. Its relative package filenames resolve to the sibling assets. HTTPS redirects are supported, and the catalog's exact bytes/SHA-256 are checked before installation.
5. Test an install, disable, remove and reinstall from a clean app folder. Do not replace an existing version's assets silently; publish a new version/tag with a newly measured catalog.

Versioned catalog URLs avoid mixing a cached catalog with different assets. Independent tool releases can later use their own tags, keeping large model updates separate from viewer updates. A stable discovery catalog should point to immutable versioned package URLs. Publishing or configuring a hosted catalog has not been performed locally.

## Shared integration work

Current function-tool protocols return text (`zxing-json-v1`, `tesseract-text-v1`). Image optimizers and image-producing tools first need an explicit result protocol, private temporary output, bounded result decoding and a preview. Outputs should be exported as a new file or applied through the existing document undo path; the tool must not overwrite the user's source directly. Launch through QProcess with fixed argument lists, explicit cancellation and timeout, and no startup daemon. Reuse the existing separate tool-pack storage and installer.

Adding an executable or a catalog row alone does not add a supported action: ToolStore currently accepts only the two recognition protocols. A new tool requires its narrow adapter, result validation and a UI command before it can be advertised as installable. Recommended sequence: Oxipng first, then Gifsicle or Potrace; prototype native background removal separately before publishing a model pack.
