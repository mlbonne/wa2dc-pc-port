// PC port shim: the Google Play OBB downloader is Android-only. Off Android
// ResourceMan/AndroidTop never call these methods; keep the API shape so
// Assembly-CSharp keeps linking.
public class GooglePlayDownloader
{
    public static bool RunningOnAndroid()
    {
        return false;
    }

    public static string GetExpansionFilePath()
    {
        return null;
    }

    public static string GetMainOBBPath(string expansionFilePath)
    {
        return null;
    }

    public static string GetPatchOBBPath(string expansionFilePath)
    {
        return null;
    }

    public static void FetchOBB()
    {
    }

    public static void GetKey(ref string _key)
    {
        _key = "PC";
    }
}
