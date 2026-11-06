using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;

namespace VoiceInput.App.Services;

public sealed record RobotWifiIdentity(string Pfx, string Token, string Identity)
{
    public X509Certificate2 Certificate() => new(Convert.FromBase64String(Pfx), "", X509KeyStorageFlags.UserKeySet | X509KeyStorageFlags.Exportable);
}

/// <summary>The server private key and pairing token are protected for the current Windows user.</summary>
public static class RobotWifiIdentityStore
{
    public static string FilePath => Path.Combine(AppConfig.DirectoryPath, "secrets", "robot-wifi.bin");
    public static RobotWifiIdentity LoadOrCreate(string? path = null)
    {
        path ??= FilePath;
        if (File.Exists(path))
            return JsonSerializer.Deserialize<RobotWifiIdentity>(RobotEndpointStore.Transform(File.ReadAllBytes(path), false))
                ?? throw new IOException("Pairing identity is invalid");
        using var rsa = RSA.Create(2048);
        var request = new CertificateRequest("CN=CodexVoiceInputRobot", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyCertSign, true));
        request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection { new("1.3.6.1.5.5.7.3.1") }, true));
        using var certificate = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-2), DateTimeOffset.UtcNow.AddYears(10));
        var result = new RobotWifiIdentity(Convert.ToBase64String(certificate.Export(X509ContentType.Pfx, "")),
            Convert.ToHexString(RandomNumberGenerator.GetBytes(32)).ToLowerInvariant(), Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var encrypted = RobotEndpointStore.Transform(JsonSerializer.SerializeToUtf8Bytes(result), true);
        File.WriteAllBytes(path + ".tmp", encrypted);
        File.Move(path + ".tmp", path, false);
        return result;
    }
}
