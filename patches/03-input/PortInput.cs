using System;
using System.IO;
using System.Text;
using UnityEngine;

// PC input extras for the PVSelect dress-up screen. The arrows are game
// TouchSprites and already receive the mouse through BaseScene.CheckTouch();
// these keyboard bindings remain only as a convenience.
//
// The file sentinel below is a test hook: drop port-shot.request next to the
// executable and the game writes a render-resolution screenshot (useful in
// exclusive fullscreen, where GDI capture cannot see the game).
//
// WA2DC_RENDER_DUMP=1 and WA2DC_NO_SNOW=1 are debug-only switches, read once
// in Awake: the first logs every visible Renderer every 2 s; the second freezes
// and clears the particles so strip captures are not covered by flakes. Both
// are off by default in the shipped build.
//
// WA2DC_PV_STAGE=1..5 (or a pv-stage.request file containing the number) is a
// debug-only switch used by the sweep. When the dress-up screen is ready it
// selects that PV stage through the game's own launch path and starts the PV.
public class PortInput : MonoBehaviour
{
    private float m_DumpTimer;
    private float m_SnowTimer;
    private bool m_DumpLogged;
    private bool m_SnowCleared;
    private static bool s_DumpRenderers;
    private static bool s_NoSnow;
    private static int s_PvStage;
    private static string s_PvTab = string.Empty;
    private static bool s_PvSelectLoading;
    private static float s_PvSelectTimer;
    private static bool s_Shop;
    private static bool s_ShopLogged;
    private static float s_ShopTimer;

    public static void Ensure()
    {
        if (GameObject.Find("Port Input") != null)
        {
            return;
        }
        GameObject gameObject = new GameObject("Port Input");
        UnityEngine.Object.DontDestroyOnLoad(gameObject);
        gameObject.AddComponent<PortInput>();
    }

    private void Awake()
    {
        s_DumpRenderers = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WA2DC_RENDER_DUMP"));
        s_NoSnow = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WA2DC_NO_SNOW"));
        s_PvStage = 0;
        s_PvTab = Environment.GetEnvironmentVariable("WA2DC_PV_TAB");
        if (s_PvTab == null)
        {
            s_PvTab = string.Empty;
        }
        s_Shop = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WA2DC_SHOP"));
        string pvStage = Environment.GetEnvironmentVariable("WA2DC_PV_STAGE");
        int stageValue;
        if (!string.IsNullOrEmpty(pvStage) && int.TryParse(pvStage, out stageValue) &&
            stageValue >= 1 && stageValue <= 5)
        {
            s_PvStage = stageValue;
        }
    }

    private void Update()
    {
        PortCurtainSkin.Tick();
        TryPvCommand();
        TryShop();
        string request = Application.dataPath + "/../port-shot.request";
        if (File.Exists(request))
        {
            try
            {
                File.Delete(request);
                Application.CaptureScreenshot(Application.dataPath + "/../port-shot.png");
                Debug.Log("PortInput: screenshot written to port-shot.png");
            }
            catch (IOException)
            {
                // The test harness may be rewriting the request; retry next frame.
            }
        }
        if (s_DumpRenderers)
        {
            m_DumpTimer += Time.deltaTime;
            if (m_DumpTimer >= 2f)
            {
                m_DumpTimer = 0f;
                DumpRenderers();
            }
        }
        if (s_NoSnow)
        {
            m_SnowTimer += Time.deltaTime;
            if (m_SnowTimer >= 0.5f)
            {
                m_SnowTimer = 0f;
                ParticleSystem[] systems = UnityEngine.Object.FindObjectsOfType(typeof(ParticleSystem)) as ParticleSystem[];
                int cleared = 0;
                for (int i = 0; i < systems.Length; i++)
                {
                    // Measurement only: freeze every particle system, not just
                    // the one called "Snow", so no flake can land on a strip.
                    systems[i].enableEmission = false;
                    systems[i].Clear();
                    cleared++;
                }
                if (cleared > 0 && !m_SnowCleared)
                {
                    m_SnowCleared = true;
                    Debug.Log("PortInput: particles disabled for measurement (" + cleared + ")");
                }
            }
        }
        PVSelectController controller = UnityEngine.Object.FindObjectOfType(typeof(PVSelectController)) as PVSelectController;
        if (controller == null)
        {
            return;
        }
        if (Input.GetKeyDown(KeyCode.RightArrow))
        {
            controller.NextItem();
        }
        if (Input.GetKeyDown(KeyCode.LeftArrow))
        {
            controller.PreItem();
        }
    }

    private void TryPvCommand()
    {
        string request = Application.dataPath + "/../pv-stage.request";
        string tabRequest = Application.dataPath + "/../pv-tab.request";
        string backRequest = Application.dataPath + "/../pv-back.request";
        if (File.Exists(backRequest))
        {
            PVBaseScene pv = UnityEngine.Object.FindObjectOfType(typeof(PVBaseScene)) as PVBaseScene;
            if (pv != null)
            {
                Debug.Log("PortInput: returning from PV to PVSelect (debug switch)");
                DeleteQuietly(backRequest);
                pv.CallButton("CharaSelect");
            }
            return;
        }
        if (s_PvStage <= 0 && s_PvTab.Length == 0 && File.Exists(request))
        {
            try
            {
                string text = File.ReadAllText(request).Trim();
                int value;
                if (int.TryParse(text, out value) && value >= 1 && value <= 5)
                {
                    s_PvStage = value;
                }
            }
            catch (IOException)
            {
                // The harness may still be writing the request; retry later.
            }
        }
        if (s_PvStage <= 0 && s_PvTab.Length == 0 && File.Exists(tabRequest))
        {
            try
            {
                string text = File.ReadAllText(tabRequest).Trim();
                if (text.Length > 0)
                {
                    s_PvTab = text;
                }
            }
            catch (IOException)
            {
            }
        }
        if (s_PvStage <= 0 && s_PvTab.Length == 0)
        {
            return;
        }

        if (s_PvStage > 0)
        {
            PVSelectScene pvScene;
            if (!EnsurePvSelect(out pvScene))
            {
                return;
            }
            int pvNum = 1;
            try
            {
                pvNum = CoreData.Instance.GetSelectPVNum(GameData.Instance.PlayCharacter);
                if (pvNum <= 0)
                {
                    pvNum = 1;
                }
            }
            catch (Exception)
            {
            }
            // Same statics the game's own PVSelect -> Save() path writes, then
            // the normal scene load. This starts the PV with the saved costume.
            PVBaseScene.UserMode = false;
            PVBaseScene.StageNum = s_PvStage;
            PVBaseScene.PVNum = pvNum;
            Debug.Log("PortInput: starting PV stage " + s_PvStage + " pv=" + pvNum +
                " from PVSelectScene (debug switch)");
            s_PvStage = 0;
            DeleteQuietly(request);
            GameData.SceneChange(SceneType.PV, pvScene);
            return;
        }

        PVSelectScene tabScene;
        if (!EnsurePvSelect(out tabScene))
        {
            return;
        }
        string button = s_PvTab.StartsWith("btn_") ? s_PvTab : "btn_" + s_PvTab;
        Debug.Log("PortInput: opening PV tab " + button + " (debug switch)");
        s_PvTab = string.Empty;
        DeleteQuietly(tabRequest);
        tabScene.m_Controller.CallButton(button);
    }

    private static bool EnsurePvSelect(out PVSelectScene pvScene)
    {
        pvScene = UnityEngine.Object.FindObjectOfType(typeof(PVSelectScene)) as PVSelectScene;
        bool ready = false;
        if (pvScene != null && pvScene.m_Controller != null)
        {
            try
            {
                ready = pvScene.m_Controller.BGArray != null;
            }
            catch (Exception)
            {
            }
        }
        if (ready)
        {
            s_PvSelectLoading = false;
            return true;
        }
        if (s_PvSelectLoading)
        {
            s_PvSelectTimer += Time.deltaTime;
            if (s_PvSelectTimer > 30f)
            {
                s_PvSelectLoading = false;
            }
            return false;
        }
        BaseScene scene = CurrentScene();
        if (scene != null && !(scene is PVSelectScene))
        {
            s_PvSelectLoading = true;
            s_PvSelectTimer = 0f;
            Debug.Log("PortInput: loading PVSelect (debug switch)");
            GameData.SceneChange(SceneType.PVSelect, scene);
        }
        return false;
    }

    private static BaseScene CurrentScene()
    {
        BaseScene[] scenes = UnityEngine.Object.FindObjectsOfType(typeof(BaseScene)) as BaseScene[];
        for (int i = 0; i < scenes.Length; i++)
        {
            if (scenes[i] != null && !(scenes[i] is AndroidTop))
            {
                return scenes[i];
            }
        }
        return scenes.Length > 0 ? scenes[0] : null;
    }

    private void TryShop()
    {
        if (!s_Shop)
        {
            return;
        }
        CharaSelectScene chara = UnityEngine.Object.FindObjectOfType(typeof(CharaSelectScene)) as CharaSelectScene;
        if (chara == null)
        {
            return;
        }
        s_ShopTimer += Time.deltaTime;
        if (s_ShopTimer < 0.5f)
        {
            return;
        }
        s_ShopTimer = 0f;
        if (!s_ShopLogged)
        {
            s_ShopLogged = true;
            Debug.Log("PortInput: opening item shop (debug switch)");
        }
        chara.CallButton("btn_item_shop");
    }

    private static void DeleteQuietly(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
    }

    private static string PathOf(Transform t)
    {
        string p = t.name;
        while (t.parent != null)
        {
            t = t.parent;
            p = t.name + "/" + p;
        }
        return p;
    }

    private void DumpRenderers()
    {
        if (!m_DumpLogged)
        {
            m_DumpLogged = true;
            Debug.Log("RENDERDUMP: begin " + Application.dataPath);
        }
        Renderer[] renderers = UnityEngine.Object.FindObjectsOfType(typeof(Renderer)) as Renderer[];
        StringBuilder sb = new StringBuilder();
        sb.AppendLine("RENDERDUMP: " + Time.frameCount + " t=" + Time.time.ToString("F1"));
        for (int i = 0; i < renderers.Length; i++)
        {
            Renderer r = renderers[i];
            if (r == null || !r.enabled || !r.gameObject.active)
            {
                continue;
            }
            string mesh = "-";
            MeshFilter filter = r.GetComponent<MeshFilter>();
            if (filter != null && filter.sharedMesh != null)
            {
                Mesh m = filter.sharedMesh;
                mesh = m.name + " v=" + m.vertexCount + " colors=" + (m.colors != null && m.colors.Length > 0);
            }
            Material mat = r.sharedMaterial;
            string matName = mat != null ? mat.name : "-";
            string shader = "-";
            string tint = "-";
            string tex = "-";
            string uv = "-";
            int queue = -1;
            if (mat != null && mat.shader != null)
            {
                shader = mat.shader.name + " sup=" + mat.shader.isSupported;
                queue = mat.renderQueue;
                if (mat.HasProperty("_TintColor")) tint = mat.GetColor("_TintColor").ToString();
                else if (mat.HasProperty("_Color")) tint = mat.GetColor("_Color").ToString();
                if (mat.mainTexture != null)
                {
                    Texture2D texture = mat.mainTexture as Texture2D;
                    tex = mat.mainTexture.name + " " + mat.mainTexture.width + "x" + mat.mainTexture.height +
                        (texture != null ? " fmt=" + texture.format : "");
                }
                uv = mat.mainTextureScale.ToString() + "/" + mat.mainTextureOffset.ToString();
            }
            Vector3 screen = Vector3.zero;
            if (Camera.main != null)
            {
                screen = Camera.main.WorldToScreenPoint(r.bounds.center);
            }
            string particle = "";
            ParticleSystemRenderer psr = r as ParticleSystemRenderer;
            if (psr != null)
            {
                ParticleSystem ps = r.GetComponent<ParticleSystem>();
                if (ps != null)
                {
                    particle = " ps_count=" + ps.particleCount + " startColor=" + ps.startColor;
                }
            }
            sb.AppendLine("RENDER " + PathOf(r.transform) + " | mesh=" + mesh + " | mat=" + matName +
                " | shader=" + shader + " q=" + queue + " | tint=" + tint + " | tex=" + tex +
                " | uv=" + uv + " | bounds=" + r.bounds.size.ToString() + " | screen=" + screen.ToString() + particle);
        }
        Debug.Log(sb.ToString());
    }
}
