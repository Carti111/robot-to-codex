using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace VoiceInput.Robot;

public sealed record PairableRobot(string Device,string Name,IPAddress Address,int Port)
{
    public override string ToString()=>Name+" · "+Device[^4..].ToUpperInvariant();
}
public sealed record WifiPairingProvision(string cert,string token,string identity,string host);

public static class WifiRobotPairing
{
    public static async Task<IReadOnlyList<PairableRobot>> DiscoverAsync(CancellationToken token)
    {
        using var udp=new UdpClient(AddressFamily.InterNetwork);udp.EnableBroadcast=true;
        var found=new Dictionary<string,PairableRobot>();
        for(var attempt=0;attempt<3;attempt++) {
            await udp.SendAsync(Encoding.ASCII.GetBytes("CVI-PAIR/1"),new IPEndPoint(IPAddress.Broadcast,45833),token);
            using var window=CancellationTokenSource.CreateLinkedTokenSource(token);window.CancelAfter(700);
            while(!window.IsCancellationRequested) {
                try {
                    var packet=await udp.ReceiveAsync(window.Token);
                    if(TryParseDiscovery(packet.Buffer,packet.RemoteEndPoint.Address,out var robot)) found[robot!.Device]=robot;
                } catch(OperationCanceledException) when(!token.IsCancellationRequested){break;}
                catch(JsonException){}catch(KeyNotFoundException){}catch(InvalidOperationException){}catch(FormatException){}
            }
        }
        token.ThrowIfCancellationRequested();return found.Values.ToList();
    }
    /// <summary>Parse only public discovery metadata. This never authenticates a device or performs I/O.</summary>
    public static bool TryParseDiscovery(byte[] payload,IPAddress address,out PairableRobot? robot)
    {
        robot=null;
        if(payload.Length>512 || address.AddressFamily!=AddressFamily.InterNetwork)return false;
        try {
            using var parsed=JsonDocument.Parse(payload);var data=parsed.RootElement;
            if(data.GetProperty("v").GetInt32()!=1 || data.GetProperty("security").GetInt32()!=2 ||
               data.GetProperty("patch").GetInt32()!=1 || data.GetProperty("port").GetInt32()!=45834)return false;
            var id=data.GetProperty("device").GetString()??"";
            if(id.Length!=12 || !id.All(Uri.IsHexDigit))return false;
            var name="兼容机器人";
            if(data.TryGetProperty("name",out var label)) {
                if(label.ValueKind!=JsonValueKind.String)return false;
                var candidate=label.GetString()?.Trim()??"";
                if(candidate.Length is <1 or >64 || candidate.Any(char.IsControl))return false;
                name=candidate;
            }
            robot=new(id,name,address,45834);return true;
        }catch(Exception error) when(error is JsonException or KeyNotFoundException or InvalidOperationException or FormatException){return false;}
    }
    public static string LocalAddressFor(IPAddress robot)
    {
        using var route=new UdpClient(AddressFamily.InterNetwork);route.Connect(robot,9);
        return ((IPEndPoint)route.Client.LocalEndPoint!).Address.ToString();
    }
    public static async Task PairAsync(PairableRobot robot,string code,WifiPairingProvision provision,CancellationToken token)
    {
        if(code.Length!=6 || !code.All(c=>c is >= '0' and <= '9'))throw new ArgumentException("six_digit_code_required");
        if(robot.Port!=45834 || robot.Address.AddressFamily!=AddressFamily.InterNetwork)throw new ArgumentException("invalid_robot");
        using var handler=new HttpClientHandler {UseProxy=false,AllowAutoRedirect=false,UseCookies=true};
        using var http=new HttpClient(handler){Timeout=TimeSpan.FromSeconds(12)};
        async Task<byte[]> Post(string endpoint,byte[] payload) {
            using var body=new ByteArrayContent(payload);body.Headers.ContentType=new("application/octet-stream");
            using var request=new HttpRequestMessage(HttpMethod.Post,$"http://{robot.Address}:{robot.Port}/{endpoint}"){Content=body,Version=HttpVersion.Version11};
            using var response=await http.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,token);
            response.EnsureSuccessStatusCode();
            await using var stream=await response.Content.ReadAsStreamAsync(token);
            using var result=new MemoryStream();var buffer=new byte[512];int count;
            while((count=await stream.ReadAsync(buffer,token))!=0) {if(result.Length+count>4096)throw new IOException("pairing_response_too_large");result.Write(buffer,0,count);}
            return result.ToArray();
        }
        using var security=new EspSecurity2Client("sparkbot",code);
        var first=await Post("pair-session",security.Start());
        var second=await Post("pair-session",security.Challenge(first));
        security.Verify(second);
        var result=security.Decrypt(await Post("pair-config",security.Encrypt(JsonSerializer.SerializeToUtf8Bytes(provision))));
        using var parsed=JsonDocument.Parse(result);
        if(!parsed.RootElement.GetProperty("ok").GetBoolean())throw new IOException("pairing_not_saved");
    }
}
