using System.Security.Cryptography;
using System.Text;
using Google.Protobuf;
using Org.BouncyCastle.Crypto.Agreement.Srp;
using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Security;
using BigInteger = Org.BouncyCastle.Math.BigInteger;

namespace VoiceInput.Robot;

/// <summary>ESP-IDF Protocomm Security2 wire adapter. SRP uses Bouncy Castle; AES-GCM uses .NET.</summary>
public sealed class EspSecurity2Client : IDisposable
{
    private sealed class Client : Srp6Client
    {
        public void Salt(byte[] salt, byte[] user, byte[] password) => x=Srp6Utilities.CalculateX(digest,N,salt,user,password);
    }
    private readonly Client srp=new();
    private readonly byte[] username,password,publicA;
    private byte[]? key,proof,nonce;
    private AesGcm? cipher;
    private int stage;
    public EspSecurity2Client(string username,string password)
    {
        this.username=Encoding.UTF8.GetBytes(username);this.password=Encoding.UTF8.GetBytes(password);
        srp.Init(Srp6StandardGroups.rfc5054_3072,new Sha512Digest(),new SecureRandom());
        // ESP-IDF requires a full-width 384-byte A. Match the official esp_prov client's retry rule.
        do {publicA=srp.GenerateClientCredentials([],this.username,this.password).ToByteArrayUnsigned();}while(publicA.Length!=384);
    }
    public byte[] Start()
    {
        if(stage++!=0)throw new InvalidOperationException("pairing_state");
        return Envelope(Proto.Message((1,0),(20,Proto.Message((1,username),(2,publicA)))));
    }
    public byte[] Challenge(byte[] response)
    {
        if(stage++!=1)throw new InvalidOperationException("pairing_state");
        var payload=Payload(response,1);var body=Proto.Bytes(payload,21);
        if(Proto.Number(body,1)!=0)throw new AuthenticationTagMismatchException("pairing_rejected");
        var b=Proto.Bytes(body,2);var salt=Proto.Bytes(body,3);
        if(b.Length is <1 or >384 || salt.Length!=16)throw new IOException("invalid_pairing_challenge");
        srp.Salt(salt,username,password);
        key=SHA512.HashData(srp.CalculateSecret(new BigInteger(1,b)).ToByteArrayUnsigned());
        var group=Srp6StandardGroups.rfc5054_3072;
        var n=group.N.ToByteArrayUnsigned();var g=new byte[384];g[^1]=5;
        var xor=SHA512.HashData(n);var hg=SHA512.HashData(g);for(var i=0;i<xor.Length;i++)xor[i]^=hg[i];
        proof=Hash(xor,SHA512.HashData(username),salt,publicA,b,key);
        return Envelope(Proto.Message((1,2),(22,Proto.Message((1,proof)))));
    }
    public void Verify(byte[] response)
    {
        if(stage++!=2)throw new InvalidOperationException("pairing_state");
        var body=Proto.Bytes(Payload(response,3),23);
        if(Proto.Number(body,1)!=0 || !CryptographicOperations.FixedTimeEquals(Proto.Bytes(body,2),Hash(publicA,proof!,key!)))
            throw new AuthenticationTagMismatchException("pairing_code_incorrect");
        nonce=Proto.Bytes(body,3);
        if(nonce.Length!=12)throw new IOException("invalid_pairing_nonce");
        cipher=new AesGcm(key![..32],16);
    }
    public byte[] Encrypt(byte[] data)
    {
        if(stage!=3 || cipher is null)throw new InvalidOperationException("pairing_not_authenticated");
        var result=new byte[data.Length+16];cipher.Encrypt(nonce!,data,result.AsSpan(0,data.Length),result.AsSpan(data.Length));Increment();return result;
    }
    public byte[] Decrypt(byte[] data)
    {
        if(stage!=3 || cipher is null || data.Length<16)throw new InvalidOperationException("pairing_not_authenticated");
        var result=new byte[data.Length-16];cipher.Decrypt(nonce!,data.AsSpan(0,result.Length),data.AsSpan(result.Length),result);Increment();return result;
    }
    private void Increment()
    {
        for(var i=11;i>=8;i--)if(++nonce![i]!=0)return;
        throw new CryptographicException("pairing_nonce_exhausted");
    }
    private static byte[] Hash(params byte[][] values)
    {using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA512);foreach(var value in values)hash.AppendData(value);return hash.GetHashAndReset();}
    private static byte[] Envelope(byte[] payload)=>Proto.Message((2,2),(12,payload));
    private static byte[] Payload(byte[] response,int expected)
    {
        if(response.Length>4096 || Proto.Number(response,2)!=2)throw new IOException("security2_required");
        var payload=Proto.Bytes(response,12);
        if(Proto.Number(payload,1)!=expected)throw new IOException("unexpected_pairing_message");return payload;
    }
    public void Dispose()
    {cipher?.Dispose();CryptographicOperations.ZeroMemory(password);if(key is not null)CryptographicOperations.ZeroMemory(key);}

    // Only the documented ESP-IDF protobuf fields are used. Unknown fields are skipped by Google.Protobuf.
    public static class Proto
    {
        public static byte[] Message(params (int Field,object Value)[] fields)
        {
            using var memory=new MemoryStream();using(var writer=new CodedOutputStream(memory,true))
            foreach(var field in fields) {
                if(field.Value is byte[] bytes) {writer.WriteTag(field.Field,WireFormat.WireType.LengthDelimited);writer.WriteBytes(ByteString.CopyFrom(bytes));}
                else {writer.WriteTag(field.Field,WireFormat.WireType.Varint);writer.WriteUInt32(Convert.ToUInt32(field.Value));}
            }
            return memory.ToArray();
        }
        public static byte[] Bytes(byte[] message,int field)
        {
            using var reader=new CodedInputStream(message);uint tag;
            while((tag=reader.ReadTag())!=0) {if(tag==WireFormat.MakeTag(field,WireFormat.WireType.LengthDelimited))return reader.ReadBytes().ToByteArray();reader.SkipLastField();}
            return [];
        }
        public static uint Number(byte[] message,int field)
        {
            using var reader=new CodedInputStream(message);uint tag;
            while((tag=reader.ReadTag())!=0) {if(tag==WireFormat.MakeTag(field,WireFormat.WireType.Varint))return reader.ReadUInt32();reader.SkipLastField();}
            return 0;
        }
    }
}
