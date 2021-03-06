// v380.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "UtlSocket.h"
#include "UtlDiscovery.h"
#include "aes.h"

#pragma pack (push, 1)
struct TCommandReq {
	int32_t command;   // NEW_USERVERIFY = 1167
	union {
		struct {
			uint32_t unknown1; // 120 = LoginFromServerEX, 1002 = LoginFromMRServerEX, 1022 = our
			uint8_t unknown2;  // 1, 2=our
			uint32_t unknown3; // 10 = LoginFromServerEX, 11 = LoginFromMRServerEX, 1=our
			uint32_t deviceId;
			char hostDateTime[32]; // optional - maybe related to unk1/2, on v380 pc this is a domain name
			char username[32];
			char password[32]; // consists of randomkey:password
		} login;
		struct {
			uint32_t deviceId;   // 4:  +292
			uint32_t unknown1;   // 8:  +284    = HSPI_V_StartPreview a4
			uint16_t unknown2;   // 12: hardcoded to 20
			uint32_t authTicket; // 14: +304
			uint32_t unknown3;   // 18: unused
			uint32_t unknown4;   // 22: *(+344) == !0 + 4096 (v14 + 4096) "0x1001" audio related?
			uint32_t unknown5;   // 26: +288    = sound related?
			uint32_t unknown6;   // 30: unused
		} streamLogin_lan;
		struct {
			uint32_t unknown1; // 1022
			char domain[32];
			uint8_t unknown2[18];
			uint16_t camPort;    // 58: v15 + 180 = 0x60 0x22 0x31 0x37 "8800"
			uint16_t unknown3;   // 60: This is not used (corrupted overlapped with domain)
			uint32_t deviceId;   // 62: v51
			uint32_t authTicket; // 66: v49
			uint32_t session;    // 70: v52
			uint32_t unknown4;   // 74: v57 = 0
			uint8_t unknown5;    // 78: = 20 hardcoded
			uint32_t unknown6;   // 79: v14 = 0x1001
			uint32_t unknown7;   // 83: v56 = 0
		} streamLogin_cloud;
		struct {
			uint32_t unknown1;
		} streamStart;
	} u;
};

struct TLoginResp {
	int32_t command;
	int32_t loginResult;
	int32_t resultValue;
	uint8_t version;
	uint32_t authTicket;
	uint32_t session;
	uint8_t deviceType;
	uint8_t camType;
	uint16_t vendorId;
	uint16_t isDomainExists;
	char domain[32];
	int32_t recDevId;
	uint8_t nChannels;
	uint8_t nAudioPri;
	uint8_t nVideoPri;
	uint8_t nSpeaker;
	uint8_t nPtzPri;
	uint8_t nReversePri;
	uint8_t nPtzXPri;
	uint8_t nPtzXCount;
	char settings[32];
	uint16_t panoX;
	uint16_t panoY;
	uint16_t panoRad; // 107
	uint32_t unknown1;
	uint8_t canUpdateDevice; // 112 (canUpdateDevice = [112] == 2)
};

struct TStreamLogin301 {
	int32_t command; // 401
	int32_t v21;
	uint16_t v73;
	uint32_t width;
	uint32_t height;
};
#pragma pack (pop)

const uint8_t cam_select[] = { 0x01, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t ptz_right[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe9, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_left[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xea, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_up[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xeb, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t ptz_down[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xec, 0x03, 0x00, 0x00, 0x01, 0x00 };
const uint8_t send_cmd[] = { 0xaa, 0x00, 0x00, 0x00, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00 };

bool readKey(bool& up, bool& down, bool& left, bool& right);


std::string generateRandomPrintable(size_t len)
{
	char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*()_+-=";
	int nSet = sizeof(set);

	std::string s;
	s.resize(len);

	std::unique_ptr<char> c(new char);
	srand((unsigned int)(size_t)c.get());

	for (size_t i = 0; i < len; i++) {
		s[i] = set[rand() % nSet];
	}

	return s;
}

void GeneratePassword(std::vector<uint8_t>& output, const std::string& password)
{
	const size_t nRandomKey = 16;
	std::string randomKey = generateRandomPrintable(nRandomKey);
	const char* staticKey = "macrovideo+*#!^@";

	std::vector<uint8_t> paddedPassword;
	size_t pad = AES_BLOCKLEN - (password.size() % AES_BLOCKLEN);
	paddedPassword.resize(password.size() + pad);
	memcpy_s(paddedPassword.data(), paddedPassword.size(), password.c_str(), password.size());

	AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t*)staticKey);

	size_t nBlock = paddedPassword.size() / AES_BLOCKLEN;
	for (size_t i = 0; i < nBlock; i++) {
		AES_ECB_encrypt(&ctx, paddedPassword.data() + i * AES_BLOCKLEN);
	}

	AES_ctx ctx2;
	AES_init_ctx(&ctx2, (uint8_t*)randomKey.data());
	for (size_t i = 0; i < nBlock; i++) {
		AES_ECB_encrypt(&ctx2, paddedPassword.data() + i * AES_BLOCKLEN);
	}

	output.resize(nRandomKey + nBlock * AES_BLOCKLEN);
	memcpy_s(output.data(), output.size(), randomKey.c_str(), nRandomKey);
	memcpy_s(output.data() + nRandomKey, output.size() - nRandomKey, paddedPassword.data(), paddedPassword.size());
}

void printHelp(FILE* f)
{
	fprintf(f, "Usage example:\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -ip 192.168.1.2\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -mac aa:bb:cc:11:22:33\n");
	fprintf(f, "  v380 -u admin -p password -port 8800 -id 123456789\n");
	fprintf(f, "  v380 -p password -addr 192.168.1.2 | ffplay -vf \"setpts = N / (25 * TB)\" -i -\n");
	fprintf(f, "\n");
	fprintf(f, "OPTIONS:\n");
	fprintf(f, "  -u              username         (default admin)\n");
	fprintf(f, "  -p              password\n");
	fprintf(f, "  -addr           camera IP/address\n");
	fprintf(f, "  -mac            camera MAC address\n");
	fprintf(f, "  -id             camera ID\n");
	fprintf(f, "  -port           camera port      (default 8800)\n");
	fprintf(f, "  -retry          Number of connection attempt (default 5)\n");
	fprintf(f, "  --enable-ptz=0  Disable pan-tilt-zoom via keyboard press\n");
	fprintf(f, "  --discover      Discover camera\n");
	fprintf(f, "  -h              Show this help\n");
}

int main(int argc, const char* argv[])
{
	int retry = 0;

	std::string ip = "";
	std::string id = "";
	std::string mac = "";
	std::string port = "8800";
	std::string username = "admin";
	std::string password = "password";
	bool enable_ptz = true;
	bool show_help = false;
	int retryCount = 5;

	for (int i = 0; i < argc; i++)
	{
		if ((_stricmp(argv[i], "-u") == 0) && ((i + 1) < argc))
		{
			username = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-p") == 0) && ((i + 1) < argc))
		{
			password = argv[i + 1];
		}
		if ((_stricmp(argv[i], "-retry") == 0) && ((i + 1) < argc))
		{
			retryCount = atoi(argv[i + 1]);
		}
		else if ((_stricmp(argv[i], "-mac") == 0) && ((i + 1) < argc))
		{
			mac = argv[i + 1];
			auto asciitolower = [](char in)->char {
				if (in <= 'Z' && in >= 'A')
					return in - ('Z' - 'z');
				return in;
			};

			std::transform(mac.begin(), mac.end(), mac.begin(), asciitolower);
		}
		else if ((_stricmp(argv[i], "-id") == 0) && ((i + 1) < argc))
		{
			id = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-addr") == 0) && ((i + 1) < argc))
		{
			ip = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "-port") == 0) && ((i + 1) < argc))
		{
			port = argv[i + 1];
		}
		else if ((_stricmp(argv[i], "--enable-ptz=0") == 0))
		{
			enable_ptz = false;
		}
		else if ((_stricmp(argv[i], "-h") == 0) || (_stricmp(argv[i], "--help") == 0))
		{
			show_help = true;
		}
		else if ((_stricmp(argv[i], "-d") == 0) || (_stricmp(argv[i], "--discover") == 0))
		{
			UtlDiscovery socketDiscovery;
			auto vDevices = socketDiscovery.Discover();

			for (auto it = vDevices.begin(); it != vDevices.end(); ++it) {
				printf(" ID:  %s\n IP:  %s\n MAC: %s\n\n", it->devid.c_str(), it->ip.c_str(), it->mac.c_str());
			}
			return 0;
		}
	}

	if (ip.empty() && mac.empty() && id.empty()) {
		fprintf(stderr, "Camera address not set\n\n");
		printHelp(stderr);
		return 1;
	}

	if (port.empty()) {
		fprintf(stderr, "Camera port not set\n\n");
		printHelp(stderr);
		return 1;
	}

	if (show_help) {
		printHelp(stdout);
		return 0;
	}

	std::vector<uint8_t> ptzcmd(send_cmd, send_cmd + sizeof(send_cmd));

#ifdef _WIN32
	_setmode(_fileno(stdout), O_BINARY);
#endif

	while (retry++ < retryCount || retryCount == 0)
	{
		try
		{
			UtlSocket socketAuth;
			UtlSocket socketStream;

			std::vector<uint8_t> buf;
			std::vector<uint8_t> hdr;
			std::vector<uint8_t> vframe;

			buf.reserve(500);
			hdr.reserve(12);
			vframe.reserve(8192);

			{
				UtlDiscovery socketDiscovery;
				auto vDevices = socketDiscovery.Discover();

				for (auto it = vDevices.begin(); it != vDevices.end(); ++it) {
					if (ip.size() && ip == it->ip) {
						id = it->devid;
						break;
					}
					if (id.size() && id == it->devid) {
						ip = it->ip;
						break;
					}
					if (mac.size() && mac == it->mac) {
						ip = it->ip;
						id = it->devid;
						break;
					}
				}

				if (ip.empty()) {
					fprintf(stderr, "Unable to find camera with specified mac/id\n");
					continue;
				}
			}
			
			socketAuth.Connect(ip, port);

			std::vector<uint8_t> pw;
			GeneratePassword(pw, password);

			buf.resize(256, 0);
			TCommandReq* req = reinterpret_cast<TCommandReq*>(buf.data());
			TLoginResp resp;

			req->command = 1167;
			req->u.login.deviceId = stoi(id);
			req->u.login.unknown1 = 1022;
			req->u.login.unknown2 = 2;
			req->u.login.unknown3 = 1;
			memcpy_s(req->u.login.username, 32, username.c_str(), username.size());
			memcpy_s(req->u.login.password, 32, pw.data(), pw.size());

			socketAuth.Send(buf);
			socketAuth.Recv(buf, 256, 5000);
			socketAuth.Close();

			resp = *reinterpret_cast<TLoginResp*>(buf.data());
			if (resp.command == 1168) {
				if (resp.loginResult == 1001) {
					//fprintf(stderr, "%x", resp->resultValue);
				} else if (resp.loginResult == 1011) {
					fprintf(stderr, "Invalid username\n");
					return resp.loginResult;
				} else if (resp.loginResult == 1012) {
					fprintf(stderr, "Invalid password\n");
					return resp.loginResult;
				} else if (resp.loginResult == 1018) {
					fprintf(stderr, "Invalid device id\n");
					return resp.loginResult;
				}
			}

			socketStream.Connect(ip, port);
			socketStream.DisableNagle();

			std::fill(buf.begin(), buf.end(), 0);
			req->command = 301; // stream login
			req->u.streamLogin_lan.deviceId = stoi(id);
			req->u.streamLogin_lan.unknown1 = 0;
			req->u.streamLogin_lan.unknown2 = 0x14;   // hardcoded in HSPC_PreviewDLL.dll, maybe fps?
			req->u.streamLogin_lan.authTicket = resp.authTicket;
			req->u.streamLogin_lan.unknown4 = 4096 + 1; // not sure
			req->u.streamLogin_lan.unknown5 = 0; // not sure

			socketStream.Send(buf);
			if (socketStream.Recv(buf, 412, 5000) < 16) {
				fprintf(stderr, "Login response: expected >= 16 bytes\n");
				continue;
			}

			TStreamLogin301 loginResp = *reinterpret_cast<TStreamLogin301*>(buf.data());
			if (loginResp.command != 401) {
				fprintf(stderr, "Login response: expected 401, got %d\n", loginResp.command);
				continue;
			}

			if (loginResp.v21 == -11 || loginResp.v21 == -12) {
				fprintf(stderr, "Login response: unsupported %d, continuing\n", loginResp.v21);
			} else if (loginResp.v21 != 402 && loginResp.v21 != 1001) {
				fprintf(stderr, "Login response: unsupported %d\n", loginResp.v21);
				continue;
			}

			req->command = 303; // stream start
			req->u.streamStart.unknown1 = loginResp.v21;
			socketStream.Send(buf);

			buf.clear();

			bool exitloop = false;
			while (socketStream.Recv(hdr, 12, 5000) >= 12 && !exitloop)
			{
				switch (hdr[0])
				{
				case 0x7f:
				{
					uint16_t totalFrame = *(uint16_t*)&hdr[3];
					uint16_t curFrame = *(uint16_t*)&hdr[5];
					uint16_t len = *(uint16_t*)&hdr[7];
					uint8_t type = hdr[1];

					if (len > 500 || !totalFrame || curFrame > totalFrame) {
						fprintf(stderr, "Sanity check failed, should bail out\n");
						break;
					}

					buf.resize(len);
					int n = 0;
					while (n < len) {
						n += socketStream.Recv(buf.data() + n, len - n, 5000);
					}

					retry = 0;

					switch (type)
					{
					case 0x00:
						vframe.insert(vframe.end(), buf.begin(), buf.end());
						if (curFrame == totalFrame - 1) {
							fwrite(vframe.data(), 1, vframe.size(), stdout);
							fflush(stdout);
							vframe.clear();
						}
						break;

					case 0x01:
						// Video
						vframe.insert(vframe.end(), buf.begin(), buf.end());
						if (curFrame == totalFrame - 1) {
							fwrite(vframe.data(), 1, vframe.size(), stdout);
							fflush(stdout);
							vframe.clear();
						}
						break;

					case 0x16:
						// Audio
						// sox -t ima -r 8000 -e ms-adpcm C:\Users\Syahmi\Desktop\v380\stream.adts -e signed-integer -b 16 out.wav

						//vframe.insert(vframe.end(), buf.begin(), buf.end());
						//if (curFrame == totalFrame - 1) {
						//	fwrite(vframe.data(), 1, vframe.size(), stdout);
						//}
						break;
					}

					break;
				}
				case 0x1f:
					// Not sure what this does yet
					fprintf(stderr, "Unparsed 0x1f data\n");
					break;
				case 0x6f:
					Sleep(20);
					break;
				default:
					exitloop = true;
					fprintf(stderr, "Unknown 0x%02x command\n", hdr[0]);
					break;
				}

				bool up, dn, l, r;
				if (enable_ptz && readKey(up, dn, l, r)) {
					ptzcmd[8] = 0xe8;
					ptzcmd[10] = 0xe8;
					if (up) ptzcmd[10] = 0xeb;
					if (dn) ptzcmd[10] = 0xec;
					if (l) ptzcmd[8] = 0xea;
					if (r) ptzcmd[8] = 0xe9;
					socketStream.Send(ptzcmd);
				}
			}
		}
		catch (const std::exception& ex)
		{
			fprintf(stderr, "%s\n", ex.what());
		}
	}

    return 0;
}

bool readKey(bool& up, bool& down, bool& left, bool& right)
{
	bool ret = false;

	up = false;
	down = false;
	left = false;
	right = false;

#ifdef _WIN32
	if (GetForegroundWindow() == GetConsoleWindow())
	{
		if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0)
		{
			right = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0)
		{
			left = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0)
		{
			up = true;
			ret = true;
		}
		if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0)
		{
			down = true;
			ret = true;
		}
	}
#endif

	return ret;
}