using System.Security.Cryptography;
using System.Text;
using Org.BouncyCastle.Crypto.Agreement.Srp;
using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Security;
using VoiceInput.Robot;
using BigInteger=Org.BouncyCastle.Math.BigInteger;
using Proto=VoiceInput.Robot.EspSecurity2Client.Proto;

internal static class PairingContract
{
    private static void Check(bool value,string message){if(!value)throw new InvalidOperationException(message);}
    private static byte[] Hash(params byte[][] values){using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA512);foreach(var v in values)hash.AppendData(v);return hash.GetHashAndReset();}
    private sealed class Server
    {
        private readonly Srp6Server srp=new();
        private readonly byte[] salt=RandomNumberGenerator.GetBytes(16),user=Encoding.ASCII.GetBytes("sparkbot");
        public byte[] A=[],B=[],Key=[],Proof=[];
        public byte[] Challenge(byte[] request,string code)
        {
            A=Proto.Bytes(Proto.Bytes(Proto.Bytes(request,12),20),2);Check(A.Length==384,"full-width public key");
            var group=Srp6StandardGroups.rfc5054_3072;
            var verifier=new Srp6VerifierGenerator();verifier.Init(group.N,group.G,new Sha512Digest());
            srp.Init(group.N,group.G,verifier.GenerateVerifier(salt,user,Encoding.ASCII.GetBytes(code)),new Sha512Digest(),new SecureRandom());
            B=srp.GenerateServerCredentials().ToByteArrayUnsigned();
            Key=SHA512.HashData(srp.CalculateSecret(new BigInteger(1,A)).ToByteArrayUnsigned());
            var g=new byte[384];g[^1]=5;var xor=SHA512.HashData(group.N.ToByteArrayUnsigned());var hg=SHA512.HashData(g);
            for(var i=0;i<xor.Length;i++)xor[i]^=hg[i];
            Proof=Hash(xor,SHA512.HashData(user),salt,A,B,Key);
            return Proto.Message((2,2),(12,Proto.Message((1,1),(21,Proto.Message((2,B),(3,salt))))));
        }
        public byte[] Response(byte[] clientProof,byte[] nonce)
        {
            var proof=Proto.Bytes(Proto.Bytes(Proto.Bytes(clientProof,12),22),1);
            Check(CryptographicOperations.FixedTimeEquals(proof,Proof),"client code evidence");
            return Proto.Message((2,2),(12,Proto.Message((1,3),(23,Proto.Message((2,Hash(A,Proof,Key)),(3,nonce))))));
        }
    }
    public static Task<int> RunAll()
    {
        using(var client=new EspSecurity2Client("sparkbot","123456")) {
            var server=new Server();var proof=client.Challenge(server.Challenge(client.Start(),"123456"));var nonce=RandomNumberGenerator.GetBytes(12);
            client.Verify(server.Response(proof,nonce));
            var plaintext=Encoding.UTF8.GetBytes("certificate and pairing token fixture");var encrypted=client.Encrypt(plaintext);
            using var cipher=new AesGcm(server.Key[..32],16);var decoded=new byte[plaintext.Length];
            cipher.Decrypt(nonce,encrypted.AsSpan(0,plaintext.Length),encrypted.AsSpan(plaintext.Length),decoded);Check(decoded.SequenceEqual(plaintext),"authenticated configuration");
            for(var i=11;i>=8;i--)if(++nonce[i]!=0)break;
            var reply=Encoding.ASCII.GetBytes("{\"ok\":true}");var secured=new byte[reply.Length+16];
            cipher.Encrypt(nonce,reply,secured.AsSpan(0,reply.Length),secured.AsSpan(reply.Length));
            Check(client.Decrypt(secured).SequenceEqual(reply),"official patch1 nonce counter");
        }
        Console.WriteLine("PASS official Security2 SRP-3072/SHA512 evidence and AES-GCM patch1 exchange");
        using(var client=new EspSecurity2Client("sparkbot","654321")) {
            var server=new Server();var proof=client.Challenge(server.Challenge(client.Start(),"123456"));
            var actual=Proto.Bytes(Proto.Bytes(Proto.Bytes(proof,12),22),1);Check(!actual.SequenceEqual(server.Proof),"wrong code rejected");
            var fake=Proto.Message((2,2),(12,Proto.Message((1,3),(23,Proto.Message((2,new byte[64]),(3,new byte[12]))))));
            var denied=false;try{client.Verify(fake);}catch(AuthenticationTagMismatchException){denied=true;}
            Check(denied,"no configuration before mutual proof");
        }
        Console.WriteLine("PASS wireless wrong code and forged device proof cannot authorize configuration");
        using(var client=new EspSecurity2Client("sparkbot","123456")) {
            var rejected=false;client.Start();try{client.Challenge(Proto.Message((2,1)));}catch(IOException){rejected=true;}
            Check(rejected,"no insecure protocol downgrade");
        }
        using(var client=new EspSecurity2Client("sparkbot","123456")) {
            var server=new Server();var proof=client.Challenge(server.Challenge(client.Start(),"123456"));client.Verify(server.Response(proof,new byte[12]));
            var rejected=false;try{client.Decrypt(new byte[32]);}catch(AuthenticationTagMismatchException){rejected=true;}
            Check(rejected,"tampered configuration rejected");
        }
        Console.WriteLine("PASS pairing rejects security downgrade and tampered encrypted payloads");
        return Task.FromResult(3);
    }
}
