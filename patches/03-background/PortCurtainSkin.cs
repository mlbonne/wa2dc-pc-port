using System;
using System.IO;
using System.Threading;
using UnityEngine;

// PC port: paint the game's own Left/Right letterbox curtains with the supplied
// background art. The game builds those curtains in BaseScene._BuildDropCurtain()
// and rebuilds them on resize; DropCurtain.Initalize calls Apply() after every
// build, so the art follows resize/fullscreen.
//
// The strip texture is allocated at a power-of-two size. Unity 3.5 resamples a
// non-power-of-two runtime texture to the next power of two when it uploads it,
// which filtered the strip even with Point sampling. The visible w x h strip is
// written into the bottom-left corner and the quad's UVs cover only that region;
// the rest is filled with a copy of the edge pixels so nothing bleeds.
//
// The pixel math runs on a worker thread and the finished Color32[] is uploaded
// on the main thread, so a resize never blocks on the resample. The old picture
// stays on screen (stretched to the new rect) until the new one is ready.
public static class PortCurtainSkin
{
    private const string ArtName = "Port Curtain Art";
    private const float FaceCenterLeft = 265f;   // px into the left 564-px half
    private const float FaceCenterRight = 316f;  // px into the right 564-px half
    private const float FaceCenterFromTop = 0.30f;

    private static Texture2D s_Source;
    private static Color32[] s_Opaque;
    private static int s_SourceWidth;
    private static int s_SourceHeight;
    private static bool s_Loaded;
    private static bool s_Logged;
    private static Shader s_Shader;
    private static float s_OffsetX = float.NaN;
    private static float s_OffsetY = float.NaN;
    private static float s_RectTimer;
    private static int s_LastScreenWidth;
    private static int s_LastScreenHeight;

    private sealed class Side
    {
        public bool IsLeft;
        public Texture2D Texture;
        public int TexWidth;
        public int TexHeight;
        public int PotWidth;
        public int PotHeight;
        public Color32[] PotBuffer;
        public int DesiredWidth;
        public int DesiredHeight;
        public int RequestGen;
        public bool Building;
        public Color32[] ReadyPixels;
        public string ReadyError;
        public int ReadyGen;
        public int ReadyWidth;
        public int ReadyHeight;
        public int LastFailedGen = -1;
        public bool ErrorLogged;
        public volatile bool ReadyFlag;
        public Transform Art;
        public Transform Curtain;
        public MeshFilter Filter;
        public MeshRenderer Renderer;
        public bool HasRect;
        public float ScreenLeft;
        public float ScreenRight;
        public float ScreenBottom;
        public float ScreenTop;
        public int LastScreenWidth;
        public int LastScreenHeight;
    }

    private sealed class BuildJob
    {
        public bool Left;
        public int Width;
        public int Height;
        public int PotWidth;
        public int PotHeight;
        public int Gen;
        public Color32[] Source;
        public int SourceWidth;
        public int SourceHeight;
        public float SourceX0;
        public float SourceY0;
        public float SourceXScale;
        public float SourceYScale;
    }

    private static readonly Side s_Left = new Side();
    private static readonly Side s_Right = new Side();

    public static void Apply(DropCurtain curtain)
    {
        if (curtain == null)
        {
            return;
        }
        if (curtain.type == DropCurtain.DropCurtainType.Up || curtain.type == DropCurtain.DropCurtainType.Down)
        {
            return;
        }
        EnsureArt();
        if (s_Opaque == null)
        {
            return;
        }

        Transform parent = curtain.transform;
        Transform art = parent.Find(ArtName);
        if (art == null)
        {
            GameObject quad = new GameObject(ArtName);
            quad.layer = parent.gameObject.layer;
            quad.transform.parent = parent;
            quad.transform.localPosition = new Vector3(0f, 0.01f, 0f);
            quad.transform.localRotation = Quaternion.identity;
            quad.transform.localScale = Vector3.one;
            quad.AddComponent<MeshFilter>();
            quad.AddComponent<MeshRenderer>();
            art = quad.transform;
        }
        Collider oldCollider = art.GetComponent<Collider>();
        if (oldCollider != null)
        {
            UnityEngine.Object.Destroy(oldCollider);
        }
        MeshFilter meshFilter = art.GetComponent<MeshFilter>();
        if (meshFilter == null)
        {
            meshFilter = art.gameObject.AddComponent<MeshFilter>();
        }
        MeshRenderer renderer = art.GetComponent<MeshRenderer>();
        if (renderer == null)
        {
            renderer = art.gameObject.AddComponent<MeshRenderer>();
        }
        if (s_Shader == null)
        {
            s_Shader = Shader.Find("Unlit/Texture");
            if (s_Shader == null)
            {
                s_Shader = Shader.Find("Unlit/Transparent");
            }
        }
        if (s_Shader == null)
        {
            return;
        }

        float screenLeft;
        float screenRight;
        float screenTop;
        float screenBottom;
        float visibleWidth;
        float visibleHeight;
        if (!MeasureVisible(parent, out screenLeft, out screenRight, out screenTop, out screenBottom,
                out visibleWidth, out visibleHeight))
        {
            return;
        }

        bool left = curtain.type == DropCurtain.DropCurtainType.Left;
        Side side = left ? s_Left : s_Right;
        side.IsLeft = left;
        side.Art = art;
        side.Curtain = parent;
        side.Filter = meshFilter;
        side.Renderer = renderer;
        side.HasRect = true;
        side.ScreenLeft = screenLeft;
        side.ScreenRight = screenRight;
        side.ScreenTop = screenTop;
        side.ScreenBottom = screenBottom;
        side.LastScreenWidth = Screen.width;
        side.LastScreenHeight = Screen.height;

        int targetWidth = Mathf.Max(1, Mathf.RoundToInt(visibleWidth));
        int targetHeight = Mathf.Max(1, Mathf.RoundToInt(visibleHeight));
        if (side.DesiredWidth != targetWidth || side.DesiredHeight != targetHeight || side.RequestGen == 0)
        {
            side.DesiredWidth = targetWidth;
            side.DesiredHeight = targetHeight;
            side.RequestGen++;
        }

        // Draw immediately with whatever texture we have (the old picture
        // stretched to the new rect, or nothing on the very first build).
        ApplyToArt(side);
        MaybeStartBuild(side);

        if (!s_Logged)
        {
            s_Logged = true;
            Debug.Log("PortCurtainSkin: source=" + s_SourceWidth + "x" + s_SourceHeight
                + " strip=" + targetWidth + "x" + targetHeight);
        }
    }

    // Called once per frame by PortInput.Update: uploads finished worker
    // results on the main thread and starts the next queued size.
    public static void Tick()
    {
        // React immediately to a real screen-size change; the 0.25 s poll stays
        // as a fallback for camera/curtain moves without a Screen change.
        int screenWidth = Screen.width;
        int screenHeight = Screen.height;
        if (screenWidth != s_LastScreenWidth || screenHeight != s_LastScreenHeight)
        {
            s_LastScreenWidth = screenWidth;
            s_LastScreenHeight = screenHeight;
            RefreshRect(s_Left);
            RefreshRect(s_Right);
        }
        s_RectTimer += Time.deltaTime;
        if (s_RectTimer >= 0.25f)
        {
            s_RectTimer = 0f;
            RefreshRect(s_Left);
            RefreshRect(s_Right);
        }
        TickSide(s_Left);
        TickSide(s_Right);
    }

    private static void TickSide(Side side)
    {
        if (side.ReadyFlag)
        {
            Color32[] pixels;
            string error;
            int gen;
            int width;
            int height;
            lock (side)
            {
                pixels = side.ReadyPixels;
                error = side.ReadyError;
                gen = side.ReadyGen;
                width = side.ReadyWidth;
                height = side.ReadyHeight;
                side.ReadyPixels = null;
                side.ReadyError = null;
                side.ReadyFlag = false;
            }
            side.Building = false;
            if (pixels == null)
            {
                // A worker exception must never leave Building set or reach Upload.
                side.LastFailedGen = gen;
                if (!string.IsNullOrEmpty(error) && !side.ErrorLogged)
                {
                    side.ErrorLogged = true;
                    Debug.LogError("PortCurtainSkin: strip build failed: " + error);
                }
                MaybeStartBuild(side);
                return;
            }
            if (gen == side.RequestGen && width == side.DesiredWidth && height == side.DesiredHeight &&
                side.Art != null)
            {
                Upload(side, pixels, width, height);
                ApplyToArt(side);
            }
        }
        MaybeStartBuild(side);
    }

    private static void RefreshRect(Side side)
    {
        if (side.Curtain == null || !side.HasRect || side.Art == null)
        {
            return;
        }
        float left;
        float right;
        float top;
        float bottom;
        float visibleWidth;
        float visibleHeight;
        if (!MeasureVisible(side.Curtain, out left, out right, out top, out bottom,
                out visibleWidth, out visibleHeight))
        {
            return;
        }
        bool same = Screen.width == side.LastScreenWidth && Screen.height == side.LastScreenHeight &&
            Math.Abs(left - side.ScreenLeft) < 0.01f && Math.Abs(right - side.ScreenRight) < 0.01f &&
            Math.Abs(top - side.ScreenTop) < 0.01f && Math.Abs(bottom - side.ScreenBottom) < 0.01f;
        if (same)
        {
            return;
        }
        side.ScreenLeft = left;
        side.ScreenRight = right;
        side.ScreenTop = top;
        side.ScreenBottom = bottom;
        side.LastScreenWidth = Screen.width;
        side.LastScreenHeight = Screen.height;
        int width = Mathf.Max(1, Mathf.RoundToInt(visibleWidth));
        int height = Mathf.Max(1, Mathf.RoundToInt(visibleHeight));
        if (width != side.DesiredWidth || height != side.DesiredHeight)
        {
            side.DesiredWidth = width;
            side.DesiredHeight = height;
            side.RequestGen++;
        }
        ApplyToArt(side);
    }

    private static void MaybeStartBuild(Side side)
    {
        if (side.Building || !side.HasRect || s_Opaque == null)
        {
            return;
        }
        if (side.Texture != null && side.TexWidth == side.DesiredWidth && side.TexHeight == side.DesiredHeight)
        {
            return;
        }
        if (side.DesiredWidth <= 0 || side.DesiredHeight <= 0)
        {
            return;
        }
        // Do not retry the same failed request every frame; a new size bumps
        // RequestGen and gets one fresh attempt.
        if (side.LastFailedGen == side.RequestGen)
        {
            return;
        }

        BuildJob job = new BuildJob();
        job.Left = side.IsLeft;
        job.Width = side.DesiredWidth;
        job.Height = side.DesiredHeight;
        job.PotWidth = NextPowerOfTwo(side.DesiredWidth);
        job.PotHeight = NextPowerOfTwo(side.DesiredHeight);
        job.Gen = side.RequestGen;
        job.Source = s_Opaque;
        job.SourceWidth = s_SourceWidth;
        job.SourceHeight = s_SourceHeight;
        ComputeCrop(job);

        side.Building = true;
        side.ErrorLogged = false;
        try
        {
            ThreadPool.QueueUserWorkItem(BuildWorker, job);
        }
        catch (Exception exception)
        {
            side.Building = false;
            side.LastFailedGen = job.Gen;
            Debug.LogError("PortCurtainSkin: could not queue strip build: " + exception);
        }
    }

    private static void ComputeCrop(BuildJob job)
    {
        float halfWidth = job.SourceWidth * 0.5f;
        float halfHeight = job.SourceHeight;
        float halfAspect = halfWidth / halfHeight;
        float stripAspect = (float)job.Width / job.Height;

        float cropWidth;
        float cropHeight;
        if (stripAspect < halfAspect)
        {
            cropHeight = halfHeight;
            cropWidth = halfHeight * stripAspect;
        }
        else
        {
            cropWidth = halfWidth;
            cropHeight = halfWidth / stripAspect;
        }

        float faceCenter = (job.Left ? FaceCenterLeft : FaceCenterRight) * (job.SourceWidth / 1128f);
        float centerX = Mathf.Clamp(faceCenter, cropWidth * 0.5f, halfWidth - cropWidth * 0.5f);
        float halfStart = job.Left ? 0f : 0.5f;
        float cropX = halfStart * job.SourceWidth + (centerX - cropWidth * 0.5f);
        float cropTop = 0f;
        if (cropHeight < halfHeight)
        {
            cropTop = Mathf.Clamp(
                FaceCenterFromTop * halfHeight - cropHeight * 0.5f,
                0f,
                halfHeight - cropHeight
            );
        }
        // Texture V grows upwards; source rows are bottom-up as well.
        float cropBottomV = (halfHeight - (cropTop + cropHeight)) / halfHeight;
        float cropTopV = (halfHeight - cropTop) / halfHeight;

        job.SourceX0 = cropX;
        job.SourceY0 = cropBottomV * job.SourceHeight;
        job.SourceXScale = cropWidth / job.Width;
        job.SourceYScale = (cropTopV - cropBottomV) * job.SourceHeight / job.Height;
    }

    // Worker thread: pure array math, no Unity API calls. Any exception is
    // caught here: an unhandled ThreadPool exception would kill the process on
    // Mono 2.x, and leaving Building set would freeze the strips forever.
    private static void BuildWorker(object state)
    {
        BuildJob job = state as BuildJob;
        if (job == null)
        {
            return;
        }
        Side side = job.Left ? s_Left : s_Right;
        try
        {
            Color32[] region = BuildPixels(job);

            // Pad to the POT size here, not on the main thread. The buffer is
            // reused while the POT size is unchanged; only one build runs per
            // side and the main thread is finished with the previous buffer
            // after SetPixels32, so there is no concurrent reader/writer.
            Color32[] full;
            lock (side)
            {
                int needed = job.PotWidth * job.PotHeight;
                if (side.PotBuffer == null || side.PotBuffer.Length != needed)
                {
                    side.PotBuffer = new Color32[needed];
                }
                full = side.PotBuffer;
            }
            FillPot(full, region, job.Width, job.Height, job.PotWidth, job.PotHeight);

            lock (side)
            {
                side.ReadyPixels = full;
                side.ReadyGen = job.Gen;
                side.ReadyWidth = job.Width;
                side.ReadyHeight = job.Height;
                side.ReadyError = null;
                side.ReadyFlag = true;
            }
        }
        catch (Exception exception)
        {
            lock (side)
            {
                side.ReadyPixels = null;
                side.ReadyGen = job.Gen;
                side.ReadyWidth = job.Width;
                side.ReadyHeight = job.Height;
                side.ReadyError = exception.ToString();
                side.ReadyFlag = true;
            }
        }
    }

    private static void FillPot(
        Color32[] full,
        Color32[] region,
        int width,
        int height,
        int potWidth,
        int potHeight)
    {
        for (int y = 0; y < potHeight; y++)
        {
            int sy = y < height ? y : height - 1;
            int dstRow = y * potWidth;
            int srcRow = sy * width;
            for (int x = 0; x < potWidth; x++)
            {
                int sx = x < width ? x : width - 1;
                full[dstRow + x] = region[srcRow + sx];
            }
        }
    }

    private static Color32[] BuildPixels(BuildJob job)
    {
        int width = job.Width;
        int height = job.Height;
        Color32[] target = new Color32[width * height];

        int sourceWidth = job.SourceWidth;
        int sourceHeight = job.SourceHeight;
        Color32[] source = job.Source;
        float sourceX0 = job.SourceX0;
        float sourceY0 = job.SourceY0;
        float sourceXScale = job.SourceXScale;
        float sourceYScale = job.SourceYScale;

        // Widen the Lanczos support when downsampling so the kernel acts as an
        // antialiasing filter instead of aliasing a fixed 6-tap window. For
        // upsampling the support stays at 3 source pixels.
        float kernelX = Math.Max(1f, sourceXScale);
        float kernelY = Math.Max(1f, sourceYScale);

        int[] xStart = new int[width];
        int[] xCount = new int[width];
        float[][] xWeight = new float[width][];
        for (int tx = 0; tx < width; tx++)
        {
            float sx = sourceX0 + (tx + 0.5f) * sourceXScale - 0.5f;
            int first = (int)Math.Floor(sx - 3.0 * kernelX);
            int last = (int)Math.Ceiling(sx + 3.0 * kernelX);
            int count = last - first + 1;
            float[] weights = new float[count];
            float sum = 0f;
            for (int k = 0; k < count; k++)
            {
                float value = Lanczos((sx - (first + k)) / kernelX);
                weights[k] = value;
                sum += value;
            }
            if (sum != 0f)
            {
                for (int k = 0; k < count; k++) weights[k] /= sum;
            }
            xStart[tx] = first;
            xCount[tx] = count;
            xWeight[tx] = weights;
        }
        int[] yStart = new int[height];
        int[] yCount = new int[height];
        float[][] yWeight = new float[height][];
        for (int ty = 0; ty < height; ty++)
        {
            float sy = sourceY0 + (ty + 0.5f) * sourceYScale - 0.5f;
            int first = (int)Math.Floor(sy - 3.0 * kernelY);
            int last = (int)Math.Ceiling(sy + 3.0 * kernelY);
            int count = last - first + 1;
            float[] weights = new float[count];
            float sum = 0f;
            for (int k = 0; k < count; k++)
            {
                float value = Lanczos((sy - (first + k)) / kernelY);
                weights[k] = value;
                sum += value;
            }
            if (sum != 0f)
            {
                for (int k = 0; k < count; k++) weights[k] /= sum;
            }
            yStart[ty] = first;
            yCount[ty] = count;
            yWeight[ty] = weights;
        }

        int minSy = sourceHeight;
        int maxSy = 0;
        for (int ty = 0; ty < height; ty++)
        {
            for (int k = 0; k < yWeight[ty].Length; k++)
            {
                int iy = Clamp(yStart[ty] + k, 0, sourceHeight - 1);
                if (iy < minSy) minSy = iy;
                if (iy > maxSy) maxSy = iy;
            }
        }
        float[] temp = new float[sourceHeight * width * 3];
        for (int sy = minSy; sy <= maxSy; sy++)
        {
            int row = sy * sourceWidth;
            for (int tx = 0; tx < width; tx++)
            {
                float[] weights = xWeight[tx];
                int first = xStart[tx];
                float red = 0f, green = 0f, blue = 0f;
                for (int k = 0; k < weights.Length; k++)
                {
                    Color32 pixel = source[row + Clamp(first + k, 0, sourceWidth - 1)];
                    red += pixel.r * weights[k];
                    green += pixel.g * weights[k];
                    blue += pixel.b * weights[k];
                }
                int to = (sy * width + tx) * 3;
                temp[to] = red;
                temp[to + 1] = green;
                temp[to + 2] = blue;
            }
        }
        for (int ty = 0; ty < height; ty++)
        {
            float[] weights = yWeight[ty];
            int first = yStart[ty];
            for (int tx = 0; tx < width; tx++)
            {
                float red = 0f, green = 0f, blue = 0f;
                for (int k = 0; k < weights.Length; k++)
                {
                    int iy = Clamp(first + k, 0, sourceHeight - 1);
                    int from = (iy * width + tx) * 3;
                    red += temp[from] * weights[k];
                    green += temp[from + 1] * weights[k];
                    blue += temp[from + 2] * weights[k];
                }
                target[ty * width + tx] = new Color32(
                    (byte)Clamp((int)Math.Round(red), 0, 255),
                    (byte)Clamp((int)Math.Round(green), 0, 255),
                    (byte)Clamp((int)Math.Round(blue), 0, 255),
                    255
                );
            }
        }

        // Lead's A/B choice (b): unsharp mask amount 0.35 / radius 0.6 only when
        // the art is being downsampled. Upsampling stays a clean Lanczos.
        if (sourceXScale > 1f && sourceYScale > 1f)
        {
            ApplyUnsharp(target, width, height, 0.35f, 0.6f);
        }
        return target;
    }

    private static void ApplyUnsharp(Color32[] pixels, int width, int height, float amount, float radius)
    {
        int taps = Math.Max(1, (int)Math.Ceiling(radius * 2.0));
        float sigma = radius;
        int length = taps * 2 + 1;
        float[] kernel = new float[length];
        float sum = 0f;
        for (int i = 0; i < length; i++)
        {
            float x = i - taps;
            float value = (float)Math.Exp(-(x * x) / (2f * sigma * sigma));
            kernel[i] = value;
            sum += value;
        }
        for (int i = 0; i < length; i++) kernel[i] /= sum;

        float[] blurRed = new float[pixels.Length];
        float[] blurGreen = new float[pixels.Length];
        float[] blurBlue = new float[pixels.Length];

        // Horizontal pass, row by row (contiguous reads).
        for (int y = 0; y < height; y++)
        {
            int row = y * width;
            for (int x = 0; x < width; x++)
            {
                float sr = 0f, sg = 0f, sb = 0f;
                for (int k = 0; k < length; k++)
                {
                    int sx = Clamp(x + k - taps, 0, width - 1);
                    Color32 pixel = pixels[row + sx];
                    sr += pixel.r * kernel[k];
                    sg += pixel.g * kernel[k];
                    sb += pixel.b * kernel[k];
                }
                int to = row + x;
                blurRed[to] = sr;
                blurGreen[to] = sg;
                blurBlue[to] = sb;
            }
        }
        // Vertical pass, row by row: the five source rows are contiguous in the
        // blur arrays, so this stays cache friendly.
        for (int y = 0; y < height; y++)
        {
            int row = y * width;
            for (int x = 0; x < width; x++)
            {
                float sr = 0f, sg = 0f, sb = 0f;
                for (int k = 0; k < length; k++)
                {
                    int sy = Clamp(y + k - taps, 0, height - 1);
                    int from = sy * width + x;
                    sr += blurRed[from] * kernel[k];
                    sg += blurGreen[from] * kernel[k];
                    sb += blurBlue[from] * kernel[k];
                }
                int to = row + x;
                Color32 pixel = pixels[to];
                int r = (int)Math.Round(pixel.r + amount * (pixel.r - sr));
                int g = (int)Math.Round(pixel.g + amount * (pixel.g - sg));
                int b = (int)Math.Round(pixel.b + amount * (pixel.b - sb));
                pixels[to] = new Color32(
                    (byte)Clamp(r, 0, 255),
                    (byte)Clamp(g, 0, 255),
                    (byte)Clamp(b, 0, 255),
                    255
                );
            }
        }
    }

    private static void Upload(Side side, Color32[] full, int width, int height)
    {
        int potWidth = NextPowerOfTwo(width);
        int potHeight = NextPowerOfTwo(height);
        if (side.Texture == null || side.PotWidth != potWidth || side.PotHeight != potHeight)
        {
            if (side.Texture != null)
            {
                UnityEngine.Object.Destroy(side.Texture);
            }
            side.Texture = new Texture2D(potWidth, potHeight, TextureFormat.RGB24, false);
            side.Texture.filterMode = FilterMode.Point;
            side.Texture.wrapMode = TextureWrapMode.Clamp;
            side.PotWidth = potWidth;
            side.PotHeight = potHeight;
        }

        // The worker already padded this buffer to the POT size; the main thread
        // only uploads it, so no allocation or pad loop happens here.
        side.Texture.SetPixels32(full);
        side.Texture.filterMode = FilterMode.Point;
        side.Texture.wrapMode = TextureWrapMode.Clamp;
        side.Texture.Apply(false);
        side.TexWidth = width;
        side.TexHeight = height;
    }

    private static void ApplyToArt(Side side)
    {
        if (side.Art == null || side.Filter == null || side.Renderer == null)
        {
            return;
        }
        if (side.Texture == null)
        {
            side.Renderer.enabled = false;
            return;
        }
        Material material = side.Renderer.material;
        if (material == null || material.shader == null || material.shader != s_Shader)
        {
            material = new Material(s_Shader);
            side.Renderer.material = material;
        }
        material.mainTexture = side.Texture;
        material.mainTextureScale = Vector2.one;
        material.mainTextureOffset = Vector2.zero;
        material.color = Color.white;
        side.Renderer.enabled = true;
        side.Renderer.castShadows = false;
        side.Renderer.receiveShadows = false;

        Camera camera = FindCamera(side.Art);
        if (camera == null)
        {
            return;
        }
        float left = side.ScreenLeft;
        float right = side.ScreenRight;
        float bottom = side.ScreenBottom;
        float top = side.ScreenTop;
        float offsetX;
        float offsetY;
        GetPixelOffset(out offsetX, out offsetY);
        left -= offsetX;
        right -= offsetX;
        bottom -= offsetY;
        top -= offsetY;
        float depth = camera.WorldToScreenPoint(side.Art.position).z;
        Vector3 bl = side.Art.InverseTransformPoint(camera.ScreenToWorldPoint(new Vector3(left, bottom, depth)));
        Vector3 br = side.Art.InverseTransformPoint(camera.ScreenToWorldPoint(new Vector3(right, bottom, depth)));
        Vector3 tl = side.Art.InverseTransformPoint(camera.ScreenToWorldPoint(new Vector3(left, top, depth)));
        Vector3 tr = side.Art.InverseTransformPoint(camera.ScreenToWorldPoint(new Vector3(right, top, depth)));

        Mesh mesh = side.Filter.sharedMesh;
        if (mesh == null || mesh.name != "Port Curtain Quad")
        {
            mesh = new Mesh();
            mesh.name = "Port Curtain Quad";
            side.Filter.sharedMesh = mesh;
        }
        mesh.Clear();
        mesh.vertices = new Vector3[] { bl, br, tl, tr };
        float uMax = side.PotWidth > 0 ? (float)side.TexWidth / side.PotWidth : 1f;
        float vMax = side.PotHeight > 0 ? (float)side.TexHeight / side.PotHeight : 1f;
        mesh.uv = new Vector2[] {
            new Vector2(0f, 0f),
            new Vector2(uMax, 0f),
            new Vector2(0f, vMax),
            new Vector2(uMax, vMax)
        };
        mesh.triangles = new int[] { 0, 2, 1, 2, 3, 1 };
        mesh.RecalculateBounds();
        mesh.RecalculateNormals();
    }

    private static void GetPixelOffset(out float offsetX, out float offsetY)
    {
        if (float.IsNaN(s_OffsetX))
        {
            bool d3d = SystemInfo.graphicsDeviceVersion.IndexOf("Direct3D") >= 0;
            // Proven with the test pattern: the explicit ScreenToWorldPoint quad
            // needs +0.5 px in x and -0.5 px in y on D3D9, and no offset in GL.
            s_OffsetX = d3d ? 0.5f : 0f;
            s_OffsetY = d3d ? -0.5f : 0f;
        }
        offsetX = s_OffsetX;
        offsetY = s_OffsetY;
    }

    private static int NextPowerOfTwo(int value)
    {
        int result = 1;
        while (result < value)
        {
            result <<= 1;
        }
        return result;
    }

    private static int Clamp(int value, int min, int max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    private static float Lanczos(float x)
    {
        float ax = Math.Abs(x);
        if (ax < 0.0001f)
        {
            return 1f;
        }
        if (ax >= 3f)
        {
            return 0f;
        }
        double px = Math.PI * x;
        return (float)(3.0 * Math.Sin(px) * Math.Sin(px / 3.0) / (px * px));
    }

    private static bool MeasureVisible(
        Transform curtain,
        out float screenLeft,
        out float screenRight,
        out float screenTop,
        out float screenBottom,
        out float visibleWidth,
        out float visibleHeight
    )
    {
        screenLeft = 0f;
        screenRight = 0f;
        screenTop = 0f;
        screenBottom = 0f;
        visibleWidth = 0f;
        visibleHeight = 0f;
        Camera camera = FindCamera(curtain);
        if (camera == null)
        {
            return false;
        }

        Vector3 p00 = camera.WorldToScreenPoint(curtain.TransformPoint(new Vector3(-5f, 0f, -5f)));
        Vector3 p10 = camera.WorldToScreenPoint(curtain.TransformPoint(new Vector3(5f, 0f, -5f)));
        Vector3 p11 = camera.WorldToScreenPoint(curtain.TransformPoint(new Vector3(5f, 0f, 5f)));
        Vector3 p01 = camera.WorldToScreenPoint(curtain.TransformPoint(new Vector3(-5f, 0f, 5f)));

        float xAtU0 = (p00.x + p01.x) * 0.5f;
        float xAtU1 = (p10.x + p11.x) * 0.5f;
        float yAtV0 = (p00.y + p10.y) * 0.5f;
        float yAtV1 = (p01.y + p11.y) * 0.5f;
        if (Mathf.Approximately(xAtU1, xAtU0) || Mathf.Approximately(yAtV1, yAtV0))
        {
            return false;
        }

        float left = Mathf.Max(0f, Mathf.Min(xAtU0, xAtU1));
        float right = Mathf.Min(Screen.width, Mathf.Max(xAtU0, xAtU1));
        float bottom = Mathf.Max(0f, Mathf.Min(yAtV0, yAtV1));
        float top = Mathf.Min(Screen.height, Mathf.Max(yAtV0, yAtV1));
        // Snap to whole screen pixels so the strip quad and its texture can be
        // placed 1:1 with Point filtering at every window size.
        left = Mathf.Round(left);
        right = Mathf.Round(right);
        bottom = Mathf.Round(bottom);
        top = Mathf.Round(top);
        if (right <= left || top <= bottom)
        {
            return false;
        }
        screenLeft = left;
        screenRight = right;
        screenTop = top;
        screenBottom = bottom;
        visibleWidth = right - left;
        visibleHeight = top - bottom;
        return true;
    }

    private static Camera FindCamera(Transform curtain)
    {
        // Use the camera that actually renders the curtain's layer. The side
        // strips live on the BGCamera, not the 2:3 UICamera.
        Camera[] cameras = Camera.allCameras;
        int layerMask = 1 << curtain.gameObject.layer;
        for (int i = 0; i < cameras.Length; i++)
        {
            Camera candidate = cameras[i];
            if (candidate != null && candidate.enabled && (candidate.cullingMask & layerMask) != 0 &&
                candidate.targetTexture == null)
            {
                return candidate;
            }
        }
        UICamera uiCamera = UnityEngine.Object.FindObjectOfType(typeof(UICamera)) as UICamera;
        if (uiCamera != null)
        {
            Camera camera = uiCamera.GetComponent<Camera>();
            if (camera != null)
            {
                return camera;
            }
        }
        return Camera.main;
    }

    private static void EnsureArt()
    {
        if (s_Loaded)
        {
            return;
        }
        s_Loaded = true;
        string path = Path.Combine(Application.streamingAssetsPath, "bg.png");
        if (!File.Exists(path))
        {
            Debug.LogWarning("PortCurtainSkin: missing " + path);
            return;
        }
        try
        {
            Texture2D texture = new Texture2D(2, 2);
            if (!texture.LoadImage(File.ReadAllBytes(path)))
            {
                Debug.LogWarning("PortCurtainSkin: LoadImage failed for " + path);
                return;
            }
            s_Source = texture;
            s_SourceWidth = texture.width;
            s_SourceHeight = texture.height;
            Color32[] source = texture.GetPixels32();
            s_Opaque = new Color32[source.Length];
            for (int index = 0; index < source.Length; index++)
            {
                Color32 pixel = source[index];
                int alpha = pixel.a;
                s_Opaque[index] = new Color32(
                    (byte)((pixel.r * alpha + 127) / 255),
                    (byte)((pixel.g * alpha + 127) / 255),
                    (byte)((pixel.b * alpha + 127) / 255),
                    255
                );
            }
        }
        catch (Exception exception)
        {
            Debug.LogWarning("PortCurtainSkin: " + exception);
        }
    }
}
