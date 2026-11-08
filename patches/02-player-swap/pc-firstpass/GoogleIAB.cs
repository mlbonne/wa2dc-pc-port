using UnityEngine;

// PC port shim: the Android billing plugin is not present on Windows. These
// methods are never used on PC (StoreManager falls back to dummy products).
public class GoogleIAB
{
    public static void InitAndroidJavaObject() { }
    public static void enableLogging(bool shouldEnable) { }
    public static void init(string publicKey) { }
    public static void unbindService() { }
    public static void queryInventory(string[] skus) { }
    public static void queryPurchaseProduct() { }
    public static void purchaseProduct(string sku) { }
    public static void purchaseProduct(string sku, string developerPayload) { }
}
