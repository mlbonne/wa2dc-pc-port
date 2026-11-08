using System;
using System.IO;
using UnityEngine;

// PC port: paint the game's own Left/Right letterbox curtains with the supplied
// background art. The game builds those curtains in BaseScene._BuildDropCurtain()
// and rebuilds them on resize; DropCurtain.Initalize calls Apply() after every
// build, so the art follows resize/fullscreen.
//
// The art is opaque (bg.png composited over black), pre-scaled to the exact
// visible strip size with an area average, and drawn with Unlit/Texture so the
// backbuffer can never show through. The center viewport is never touched.
public static class PortCurtainSkin
{
    private const string ArtName = "Port Curtain Art";
    private const float FaceCenterLeft = 265f;   // px into the left 564-px half
    private const float FaceCenterRight = 316f;  // px into the right 564-px half
    private const float FaceCenterFromTop = 0.30f;

    private static Texture2D s_Source;
    private static Color32[] s_Opaque;
    private static bool s_Loaded;
    private static bool s_Logged;

    private static Texture2D s_LeftTexture;
    private static Texture2D s_RightTexture;
    private static string s_LeftKey = string.Empty;
    private static string s_RightKey = string.Empty;

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
            GameObject primitive = GameObject.CreatePrimitive(PrimitiveType.Plane);
            primitive.name = ArtName;
            Collider collider = primitive.GetComponent<Collider>();
            if (collider != null)
            {
                UnityEngine.Object.Destroy(collider);
            }
            primitive.layer = parent.gameObject.layer;
            primitive.transform.parent = parent;
            primitive.transform.localPosition = new Vector3(0f, 0.01f, 0f);
            primitive.transform.localRotation = Quaternion.identity;
            primitive.transform.localScale = Vector3.one;
            art = primitive.transform;
        }

        MeshRenderer renderer = art.GetComponent<MeshRenderer>();
        if (renderer == null)
        {
            return;
        }
        Shader shader = Shader.Find("Unlit/Texture");
        if (shader == null)
        {
            shader = Shader.Find("Unlit/Transparent");
        }
        if (shader == null)
        {
            return;
        }

        float uMin;
        float uMax;
        float vTop;
        float vBottom;
        float visibleWidth;
        float visibleHeight;
        if (!MeasureVisible(parent, out uMin, out uMax, out vTop, out vBottom, out visibleWidth, out visibleHeight))
        {
            return;
        }

        bool left = curtain.type == DropCurtain.DropCurtainType.Left;
        int targetWidth = Mathf.Max(1, Mathf.RoundToInt(visibleWidth));
        int targetHeight = Mathf.Max(1, Mathf.RoundToInt(visibleHeight));
        string key = ConstructKey(left, targetWidth, targetHeight);

        Texture2D texture;
        if (left)
        {
            if (s_LeftTexture == null || s_LeftKey != key)
            {
                Texture2D previous = s_LeftTexture;
                s_LeftTexture = BuildStrip(left, targetWidth, targetHeight, key);
                s_LeftKey = key;
                if (previous != null)
                {
                    UnityEngine.Object.Destroy(previous);
                }
            }
            texture = s_LeftTexture;
        }
        else
        {
            if (s_RightTexture == null || s_RightKey != key)
            {
                Texture2D previous = s_RightTexture;
                s_RightTexture = BuildStrip(left, targetWidth, targetHeight, key);
                s_RightKey = key;
                if (previous != null)
                {
                    UnityEngine.Object.Destroy(previous);
                }
            }
            texture = s_RightTexture;
        }
        if (texture == null)
        {
            return;
        }

        bool point = Mathf.Abs(visibleWidth - targetWidth) < 0.01f
            && Mathf.Abs(visibleHeight - targetHeight) < 0.01f;
        texture.wrapMode = TextureWrapMode.Clamp;
        texture.filterMode = point ? FilterMode.Point : FilterMode.Bilinear;

        Material material = renderer.material;
        if (material == null || material.shader == null || material.shader.name != shader.name)
        {
            material = new Material(shader);
            renderer.material = material;
        }
        material.mainTexture = texture;
        material.color = Color.white;
        renderer.enabled = true;
        renderer.castShadows = false;
        renderer.receiveShadows = false;

        // The strip texture is exactly the visible rect, so the visible curtain
        // region maps to the full texture at one texel per screen pixel.
        float rangeU = uMax - uMin;
        float rangeV = vBottom - vTop;
        float scaleX = 1f / rangeU;
        float offsetX = -uMin * scaleX;
        float scaleY = -1f / rangeV;
        float offsetY = 1f - scaleY * vTop;
        material.mainTextureScale = new Vector2(scaleX, scaleY);
        material.mainTextureOffset = new Vector2(offsetX, offsetY);

        if (!s_Logged)
        {
            s_Logged = true;
            Debug.Log("PortCurtainSkin: source=" + s_Source.width + "x" + s_Source.height
                + " mipmaps=" + s_Source.mipmapCount + " format=" + s_Source.format
                + " strip=" + targetWidth + "x" + targetHeight + " filter=" + (point ? "Point" : "Bilinear"));
        }
    }

    private static Texture2D BuildStrip(bool left, int width, int height, string key)
    {
        float halfWidth = s_Source.width * 0.5f;
        float halfHeight = s_Source.height;
        float halfAspect = halfWidth / halfHeight;
        float stripAspect = (float)width / height;

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

        float faceCenter = left ? FaceCenterLeft : FaceCenterRight;
        float centerX = Mathf.Clamp(faceCenter, cropWidth * 0.5f, halfWidth - cropWidth * 0.5f);
        float halfStart = left ? 0f : 0.5f;
        float cropX = halfStart * s_Source.width + (centerX - cropWidth * 0.5f);
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

        int sourceWidth = s_Source.width;
        int sourceHeight = s_Source.height;
        Color32[] target = new Color32[width * height];
        float sourceX0 = cropX;
        float sourceY0 = cropBottomV * sourceHeight;
        float sourceXScale = cropWidth / width;
        float sourceYScale = (cropTopV - cropBottomV) * sourceHeight / height;

        // Lanczos-3 downsample of the face-centred crop. At 0.83x the source is
        // still sharper than the strip, so the kernel is safe and keeps the
        // edge energy close to the ideal Lanczos reference.
        for (int ty = 0; ty < height; ty++)
        {
            float sy = sourceY0 + (ty + 0.5f) * sourceYScale - 0.5f;
            int syBase = Mathf.FloorToInt(sy);
            float syFraction = sy - syBase;
            for (int tx = 0; tx < width; tx++)
            {
                float sx = sourceX0 + (tx + 0.5f) * sourceXScale - 0.5f;
                int sxBase = Mathf.FloorToInt(sx);
                float sxFraction = sx - sxBase;
                float red = 0f;
                float green = 0f;
                float blue = 0f;
                float weight = 0f;
                for (int ky = -2; ky <= 3; ky++)
                {
                    float wy = Lanczos(syFraction - ky);
                    if (wy == 0f)
                    {
                        continue;
                    }
                    int iy = syBase + ky;
                    if (iy < 0) iy = 0;
                    if (iy >= sourceHeight) iy = sourceHeight - 1;
                    int row = iy * sourceWidth;
                    for (int kx = -2; kx <= 3; kx++)
                    {
                        float wx = Lanczos(sxFraction - kx);
                        if (wx == 0f)
                        {
                            continue;
                        }
                        int ix = sxBase + kx;
                        if (ix < 0) ix = 0;
                        if (ix >= sourceWidth) ix = sourceWidth - 1;
                        float w = wx * wy;
                        Color32 pixel = s_Opaque[row + ix];
                        red += pixel.r * w;
                        green += pixel.g * w;
                        blue += pixel.b * w;
                        weight += w;
                    }
                }
                if (weight != 0f)
                {
                    target[ty * width + tx] = new Color32(
                        (byte)Mathf.Clamp(Mathf.RoundToInt(red / weight), 0, 255),
                        (byte)Mathf.Clamp(Mathf.RoundToInt(green / weight), 0, 255),
                        (byte)Mathf.Clamp(Mathf.RoundToInt(blue / weight), 0, 255),
                        255
                    );
                }
            }
        }

        Texture2D texture = new Texture2D(width, height, TextureFormat.RGB24, false);
        texture.SetPixels32(target);
        texture.Apply(false);
        texture.wrapMode = TextureWrapMode.Clamp;
        texture.filterMode = FilterMode.Point;
        return texture;
    }

    private static string ConstructKey(bool left, int width, int height)
    {
        return (left ? "L" : "R") + ":" + width + "x" + height;
    }

    private static float Lanczos(float x)
    {
        if (Mathf.Abs(x) < 0.0001f)
        {
            return 1f;
        }
        if (x <= -3f || x >= 3f)
        {
            return 0f;
        }
        float px = Mathf.PI * x;
        return 3f * Mathf.Sin(px) * Mathf.Sin(px / 3f) / (px * px);
    }

    private static bool MeasureVisible(
        Transform curtain,
        out float uMin,
        out float uMax,
        out float vTop,
        out float vBottom,
        out float visibleWidth,
        out float visibleHeight
    )
    {
        uMin = 0f;
        uMax = 1f;
        vTop = 0f;
        vBottom = 1f;
        visibleWidth = 0f;
        visibleHeight = 0f;
        Camera camera = FindCamera();
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
        if (right <= left || top <= bottom)
        {
            return false;
        }

        uMin = Mathf.Clamp01((left - xAtU0) / (xAtU1 - xAtU0));
        uMax = Mathf.Clamp01((right - xAtU0) / (xAtU1 - xAtU0));
        if (uMin > uMax)
        {
            float swap = uMin;
            uMin = uMax;
            uMax = swap;
        }
        float vAtTop = (top - yAtV0) / (yAtV1 - yAtV0);
        float vAtBottom = (bottom - yAtV0) / (yAtV1 - yAtV0);
        vTop = Mathf.Clamp01(Mathf.Min(vAtTop, vAtBottom));
        vBottom = Mathf.Clamp01(Mathf.Max(vAtTop, vAtBottom));
        if (vBottom - vTop < 0.0001f)
        {
            return false;
        }
        visibleWidth = right - left;
        visibleHeight = top - bottom;
        return true;
    }

    private static Camera FindCamera()
    {
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
