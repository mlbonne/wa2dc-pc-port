using System;
using System.IO;

// Reads a WA2DC share.bin and prints the owned-item bitfields.
// Format (from StoreManager.GetArray/SD_Load):
//   4 bytes CRC32 (XOR key), 4 bytes seed (XOR key), 628 bytes payload
//   payload: int WP, then 13 bitfields as written in StoreManager.SetArray.
public static class DumpShareBin
{
    private sealed class MonoRandom
    {
        private const int MBIG = int.MaxValue;
        private const int MSEED = 161803398;
        private int inext;
        private int inextp;
        private readonly int[] seedArray = new int[56];

        public MonoRandom(int seed)
        {
            int num = MSEED - Math.Abs(seed);
            seedArray[55] = num;
            int num2 = 1;
            for (int i = 1; i < 55; i++)
            {
                int num3 = 21 * i % 55;
                seedArray[num3] = num2;
                num2 = num - num2;
                if (num2 < 0)
                {
                    num2 += int.MaxValue;
                }
                num = seedArray[num3];
            }
            for (int j = 1; j < 5; j++)
            {
                for (int k = 1; k < 56; k++)
                {
                    seedArray[k] -= seedArray[1 + (k + 30) % 55];
                    if (seedArray[k] < 0)
                    {
                        seedArray[k] += int.MaxValue;
                    }
                }
            }
            inext = 0;
            inextp = 31;
        }

        private double Sample()
        {
            if (++inext >= 56) inext = 1;
            if (++inextp >= 56) inextp = 1;
            int num = seedArray[inext] - seedArray[inextp];
            if (num < 0) num += int.MaxValue;
            seedArray[inext] = num;
            return (double)num * 4.656612875245797E-10;
        }

        public int Next(int minValue, int maxValue)
        {
            uint num = (uint)(maxValue - minValue);
            if (num <= 1) return minValue;
            return (int)((uint)(Sample() * (double)num) + minValue);
        }
    }

    private static readonly byte[] Key = { 236, 119, 165, 130 };
    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        uint[] table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
            {
                c = ((c & 1) != 0) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        return table;
    }

    private static uint Crc32(byte[] data)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in data)
        {
            crc = Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    private static void PrintField(string name, byte[] payload, int offset, int size)
    {
        int count = 0;
        int capacity = size * 8;
        System.Collections.Generic.List<int> ids = new System.Collections.Generic.List<int>();
        for (int bit = 0; bit < capacity; bit++)
        {
            if ((payload[offset + bit / 8] & (1 << (bit % 8))) != 0)
            {
                count++;
                ids.Add(bit);
            }
        }
        Console.WriteLine(string.Format("{0,-24} {1,3}/{2,-3} ids={3}", name, count, capacity, string.Join(",", ids.ToArray())));
    }

    public static void Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.WriteLine("usage: DumpShareBin <share.bin>");
            return;
        }
        byte[] file = File.ReadAllBytes(args[0]);
        if (file.Length < 636)
        {
            Console.WriteLine("too short: " + file.Length);
            return;
        }
        byte[] crcBytes = new byte[4];
        byte[] seedBytes = new byte[4];
        for (int i = 0; i < 4; i++)
        {
            crcBytes[i] = (byte)(file[i] ^ Key[i & 3]);
            seedBytes[i] = (byte)(file[4 + i] ^ Key[i & 3]);
        }
        uint storedCrc = BitConverter.ToUInt32(crcBytes, 0);
        int seed = BitConverter.ToInt32(seedBytes, 0);
        byte[] payload = new byte[628];
        MonoRandom random = new MonoRandom(seed);
        for (int i = 0; i < payload.Length; i++)
        {
            payload[i] = (byte)(file[8 + i] ^ random.Next(1, 127));
        }
        Console.WriteLine("file=" + args[0]);
        byte[] raw = new byte[628];
        Array.Copy(file, 8, raw, 0, raw.Length);
        // SD_Save computes the CRC over the encoded payload, not the decoded one.
        Console.WriteLine("crc_raw_ok=" + (Crc32(raw) == storedCrc) + " stored=0x" + storedCrc.ToString("X8") + " computed=0x" + Crc32(raw).ToString("X8"));
        Console.WriteLine("seed=" + seed);
        Console.WriteLine("wp=" + BitConverter.ToInt32(payload, 0));
        PrintField("ClothesKazusa", payload, 4, 8);
        PrintField("ClothesSetuna", payload, 12, 8);
        PrintField("HeadAccessoryKazusa", payload, 20, 8);
        PrintField("HeadAccessorySetuna", payload, 28, 8);
        PrintField("FaceAccessoryKazusa", payload, 36, 4);
        PrintField("FaceAccessorySetuna", payload, 40, 4);
        PrintField("BG2D", payload, 44, 8);
        PrintField("BG3D", payload, 52, 4);
        PrintField("Effect", payload, 56, 8);
        PrintField("PVKazusa", payload, 64, 4);
        PrintField("PVSetuna", payload, 68, 4);
        PrintField("PVEdit", payload, 72, 8);
        PrintField("Spt", payload, 80, 2);
    }
}
