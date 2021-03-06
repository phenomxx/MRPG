/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "config.h"
#include "network.h"
#include "huffman.h"

void CNetRecvUnpacker::Clear()
{
	m_Valid = false;
}

void CNetRecvUnpacker::Start(const NETADDR* pAddr, CNetConnection* pConnection, int ClientID)
{
	m_Addr = *pAddr;
	m_pConnection = pConnection;
	m_ClientID = ClientID;
	m_CurrentChunk = 0;
	m_Valid = true;
}

// TODO: rename this function
int CNetRecvUnpacker::FetchChunk(CNetChunk* pChunk)
{
	// Don't bother with connections that already went offline
	if(m_pConnection && m_pConnection->State() != NET_CONNSTATE_ONLINE)
	{
		Clear();
		return 0;
	}

	CNetChunkHeader Header;
	unsigned char* pEnd = m_Data.m_aChunkData + m_Data.m_DataSize;

	while (1)
	{
		unsigned char* pData = m_Data.m_aChunkData;

		// check for old data to unpack
		if (!m_Valid || m_CurrentChunk >= m_Data.m_NumChunks)
		{
			Clear();
			return 0;
		}

		// TODO: add checking here so we don't read too far
		for (int i = 0; i < m_CurrentChunk; i++)
		{
			pData = Header.Unpack(pData);
			pData += Header.m_Size;
		}

		// unpack the header
		pData = Header.Unpack(pData);
		m_CurrentChunk++;

		if (pData + Header.m_Size > pEnd)
		{
			Clear();
			return 0;
		}

		// handle sequence stuff
		if (m_pConnection && (Header.m_Flags & NET_CHUNKFLAG_VITAL))
		{
			if (Header.m_Sequence == (m_pConnection->m_Ack + 1) % NET_MAX_SEQUENCE)
			{
				// in sequence
				m_pConnection->m_Ack = (m_pConnection->m_Ack + 1) % NET_MAX_SEQUENCE;
			}
			else
			{
				// old packet that we already got
				if (CNetBase::IsSeqInBackroom(Header.m_Sequence, m_pConnection->m_Ack))
					continue;

				// out of sequence, request resend
				if (g_Config.m_Debug)
					dbg_msg("conn", "asking for resend %d %d", Header.m_Sequence, (m_pConnection->m_Ack + 1) % NET_MAX_SEQUENCE);
				m_pConnection->SignalResend();
				continue; // take the next chunk in the packet
			}
		}

		// fill in the info
		pChunk->m_ClientID = m_ClientID;
		pChunk->m_Address = m_Addr;
		pChunk->m_Flags = (Header.m_Flags & NET_CHUNKFLAG_VITAL) ? NETSENDFLAG_VITAL : 0;
		pChunk->m_DataSize = Header.m_Size;
		pChunk->m_pData = pData;
		return 1;
	}
}

// packs the data tight and sends it
void CNetBase::SendPacketConnless(NETSOCKET Socket, const NETADDR* pAddr, TOKEN Token, TOKEN ResponseToken, const void* pData, int DataSize)
{
	unsigned char aBuffer[NET_MAX_PACKETSIZE];

	dbg_assert(DataSize <= NET_MAX_PAYLOAD, "packet data size too high");
	dbg_assert((Token & ~NET_TOKEN_MASK) == 0, "token out of range");
	dbg_assert((ResponseToken & ~NET_TOKEN_MASK) == 0, "resp token out of range");

	int i = 0;
	aBuffer[i++] = ((NET_PACKETFLAG_CONNLESS << 2) & 0xfc) | (NET_PACKETVERSION & 0x03); // connless flag and version
	aBuffer[i++] = (Token >> 24) & 0xff; // token
	aBuffer[i++] = (Token >> 16) & 0xff;
	aBuffer[i++] = (Token >> 8) & 0xff;
	aBuffer[i++] = (Token) & 0xff;
	aBuffer[i++] = (ResponseToken >> 24) & 0xff; // response token
	aBuffer[i++] = (ResponseToken >> 16) & 0xff;
	aBuffer[i++] = (ResponseToken >> 8) & 0xff;
	aBuffer[i++] = (ResponseToken) & 0xff;

	dbg_assert(i == NET_PACKETHEADERSIZE_CONNLESS, "inconsistency");

	mem_copy(&aBuffer[i], pData, DataSize);
	net_udp_send(Socket, pAddr, aBuffer, i + DataSize);
}

void CNetBase::SendPacket(NETSOCKET Socket, const NETADDR* pAddr, CNetPacketConstruct* pPacket)
{
	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	int CompressedSize = -1;
	int FinalSize = -1;

	// log the data
	if (ms_DataLogSent)
	{
		int Type = 1;
		io_write(ms_DataLogSent, &Type, sizeof(Type));
		io_write(ms_DataLogSent, &pPacket->m_DataSize, sizeof(pPacket->m_DataSize));
		io_write(ms_DataLogSent, &pPacket->m_aChunkData, pPacket->m_DataSize);
		io_flush(ms_DataLogSent);
	}

	dbg_assert((pPacket->m_Token & ~NET_TOKEN_MASK) == 0, "token out of range");

	// compress if not ctrl msg
	if (!(pPacket->m_Flags & NET_PACKETFLAG_CONTROL))
		CompressedSize = ms_Huffman.Compress(pPacket->m_aChunkData, pPacket->m_DataSize, &aBuffer[NET_PACKETHEADERSIZE], NET_MAX_PAYLOAD);

	// check if the compression was enabled, successful and good enough
	if (CompressedSize > 0 && CompressedSize < pPacket->m_DataSize)
	{
		FinalSize = CompressedSize;
		pPacket->m_Flags |= NET_PACKETFLAG_COMPRESSION;
	}
	else
	{
		// use uncompressed data
		FinalSize = pPacket->m_DataSize;
		mem_copy(&aBuffer[NET_PACKETHEADERSIZE], pPacket->m_aChunkData, pPacket->m_DataSize);
		pPacket->m_Flags &= ~NET_PACKETFLAG_COMPRESSION;
	}

	// set header and send the packet if all things are good
	if (FinalSize >= 0)
	{
		FinalSize += NET_PACKETHEADERSIZE;

		int i = 0;
		aBuffer[i++] = ((pPacket->m_Flags << 2) & 0xfc) | ((pPacket->m_Ack >> 8) & 0x03); // flags and ack
		aBuffer[i++] = (pPacket->m_Ack) & 0xff; // ack
		aBuffer[i++] = (pPacket->m_NumChunks) & 0xff; // num chunks
		aBuffer[i++] = (pPacket->m_Token >> 24) & 0xff; // token
		aBuffer[i++] = (pPacket->m_Token >> 16) & 0xff;
		aBuffer[i++] = (pPacket->m_Token >> 8) & 0xff;
		aBuffer[i++] = (pPacket->m_Token) & 0xff;

		dbg_assert(i == NET_PACKETHEADERSIZE, "inconsistency");

		net_udp_send(Socket, pAddr, aBuffer, FinalSize);

		// log raw socket data
		if (ms_DataLogSent)
		{
			int Type = 0;
			io_write(ms_DataLogSent, &Type, sizeof(Type));
			io_write(ms_DataLogSent, &FinalSize, sizeof(FinalSize));
			io_write(ms_DataLogSent, aBuffer, FinalSize);
			io_flush(ms_DataLogSent);
		}
	}
}

// TODO: rename this function
int CNetBase::UnpackPacket(unsigned char* pBuffer, int Size, CNetPacketConstruct* pPacket)
{
	// log the data
	if (ms_DataLogRecv)
	{
		int Type = 0;
		io_write(ms_DataLogRecv, &Type, sizeof(Type));
		io_write(ms_DataLogRecv, &Size, sizeof(Size));
		io_write(ms_DataLogRecv, pBuffer, Size);
		io_flush(ms_DataLogRecv);
	}

	// check the size
	if (Size < NET_PACKETHEADERSIZE || Size > NET_MAX_PACKETSIZE)
	{
		if (g_Config.m_Debug)
			dbg_msg("network", "packet too small, size=%d", Size);
		return -1;
	}

	// read the packet

	pPacket->m_Flags = (pBuffer[0] & 0xfc) >> 2;
	// FFFFFFxx
	if (pPacket->m_Flags & NET_PACKETFLAG_CONNLESS)
	{
		if (Size < NET_PACKETHEADERSIZE_CONNLESS)
		{
			if (g_Config.m_Debug)
				dbg_msg("net", "connless packet too small, size=%d", Size);
			return -1;
		}

		pPacket->m_Flags = NET_PACKETFLAG_CONNLESS;
		pPacket->m_Ack = 0;
		pPacket->m_NumChunks = 0;
		int Version = pBuffer[0] & 0x3;
		// xxxxxxVV

		if (Version != NET_PACKETVERSION)
			return -1;

		pPacket->m_DataSize = Size - NET_PACKETHEADERSIZE_CONNLESS;
		pPacket->m_Token = (pBuffer[1] << 24) | (pBuffer[2] << 16) | (pBuffer[3] << 8) | pBuffer[4];
		// TTTTTTTT TTTTTTTT TTTTTTTT TTTTTTTT
		pPacket->m_ResponseToken = (pBuffer[5] << 24) | (pBuffer[6] << 16) | (pBuffer[7] << 8) | pBuffer[8];
		// RRRRRRRR RRRRRRRR RRRRRRRR RRRRRRRR
		mem_copy(pPacket->m_aChunkData, &pBuffer[NET_PACKETHEADERSIZE_CONNLESS], pPacket->m_DataSize);
	}
	else
	{
		if (Size - NET_PACKETHEADERSIZE > NET_MAX_PAYLOAD)
		{
			if (g_Config.m_Debug)
				dbg_msg("network", "packet payload too big, size=%d", Size);
			return -1;
		}

		pPacket->m_Ack = ((pBuffer[0] & 0x3) << 8) | pBuffer[1];
		// xxxxxxAA AAAAAAAA
		pPacket->m_NumChunks = pBuffer[2];
		// NNNNNNNN

		pPacket->m_DataSize = Size - NET_PACKETHEADERSIZE;
		pPacket->m_Token = (pBuffer[3] << 24) | (pBuffer[4] << 16) | (pBuffer[5] << 8) | pBuffer[6];
		// TTTTTTTT TTTTTTTT TTTTTTTT TTTTTTTT
		pPacket->m_ResponseToken = NET_TOKEN_NONE;

		if (pPacket->m_Flags & NET_PACKETFLAG_COMPRESSION)
			pPacket->m_DataSize = ms_Huffman.Decompress(&pBuffer[NET_PACKETHEADERSIZE], pPacket->m_DataSize, pPacket->m_aChunkData, sizeof(pPacket->m_aChunkData));
		else
			mem_copy(pPacket->m_aChunkData, &pBuffer[NET_PACKETHEADERSIZE], pPacket->m_DataSize);
	}

	// check for errors
	if (pPacket->m_DataSize < 0)
	{
		if (g_Config.m_Debug)
			dbg_msg("network", "error during packet decoding");
		return -1;
	}

	// set the response token (a bit hacky because this function shouldn't know about control packets)
	if (pPacket->m_Flags & NET_PACKETFLAG_CONTROL)
	{
		if (pPacket->m_DataSize >= 5) // control byte + token
		{
			if (pPacket->m_aChunkData[0] == NET_CTRLMSG_CONNECT
				|| pPacket->m_aChunkData[0] == NET_CTRLMSG_TOKEN)
			{
				pPacket->m_ResponseToken = (pPacket->m_aChunkData[1] << 24) | (pPacket->m_aChunkData[2] << 16)
					| (pPacket->m_aChunkData[3] << 8) | pPacket->m_aChunkData[4];
			}
		}
	}

	// log the data
	if (ms_DataLogRecv)
	{
		int Type = 1;
		io_write(ms_DataLogRecv, &Type, sizeof(Type));
		io_write(ms_DataLogRecv, &pPacket->m_DataSize, sizeof(pPacket->m_DataSize));
		io_write(ms_DataLogRecv, pPacket->m_aChunkData, pPacket->m_DataSize);
		io_flush(ms_DataLogRecv);
	}

	// return success
	return 0;
}


void CNetBase::SendControlMsg(NETSOCKET Socket, const NETADDR* pAddr, TOKEN Token, int Ack, int ControlMsg, const void* pExtra, int ExtraSize)
{
	CNetPacketConstruct Construct;
	Construct.m_Token = Token;
	Construct.m_Flags = NET_PACKETFLAG_CONTROL;
	Construct.m_Ack = Ack;
	Construct.m_NumChunks = 0;
	Construct.m_DataSize = 1 + ExtraSize;
	Construct.m_aChunkData[0] = ControlMsg;
	if(ExtraSize > 0)
		mem_copy(&Construct.m_aChunkData[1], pExtra, ExtraSize);

	// send the control message
	CNetBase::SendPacket(Socket, pAddr, &Construct);
}


void CNetBase::SendControlMsgWithToken(NETSOCKET Socket, const NETADDR* pAddr, TOKEN Token, int Ack, int ControlMsg, TOKEN MyToken, bool Extended)
{
	dbg_assert((Token & ~NET_TOKEN_MASK) == 0, "token out of range");
	dbg_assert((MyToken & ~NET_TOKEN_MASK) == 0, "resp token out of range");

	static unsigned char aBuf[NET_TOKENREQUEST_DATASIZE] = { 0 };
	aBuf[0] = (MyToken >> 24) & 0xff;
	aBuf[1] = (MyToken >> 16) & 0xff;
	aBuf[2] = (MyToken >> 8) & 0xff;
	aBuf[3] = (MyToken) & 0xff;
	SendControlMsg(Socket, pAddr, Token, 0, ControlMsg, aBuf, Extended ? sizeof(aBuf) : 4);
}

unsigned char* CNetChunkHeader::Pack(unsigned char* pData)
{
	pData[0] = ((m_Flags & 0x03) << 6) | ((m_Size >> 6) & 0x3F);
	pData[1] = (m_Size & 0x3F);
	if (m_Flags & NET_CHUNKFLAG_VITAL)
	{
		pData[1] |= (m_Sequence >> 2) & 0xC0;
		pData[2] = m_Sequence & 0xFF;
		return pData + 3;
	}
	return pData + 2;
}

unsigned char* CNetChunkHeader::Unpack(unsigned char* pData)
{
	m_Flags = (pData[0] >> 6) & 0x03;
	m_Size = ((pData[0] & 0x3F) << 6) | (pData[1] & 0x3F);
	m_Sequence = -1;
	if (m_Flags & NET_CHUNKFLAG_VITAL)
	{
		m_Sequence = ((pData[1] & 0xC0) << 2) | pData[2];
		return pData + 3;
	}
	return pData + 2;
}


int CNetBase::IsSeqInBackroom(int Seq, int Ack)
{
	int Bottom = (Ack - NET_MAX_SEQUENCE / 2);
	if (Bottom < 0)
	{
		if (Seq <= Ack)
			return 1;
		if (Seq >= (Bottom + NET_MAX_SEQUENCE))
			return 1;
	}
	else
	{
		if (Seq <= Ack && Seq >= Bottom)
			return 1;
	}

	return 0;
}

IOHANDLE CNetBase::ms_DataLogSent = 0;
IOHANDLE CNetBase::ms_DataLogRecv = 0;
CHuffman CNetBase::ms_Huffman;

void CNetBase::OpenLog(IOHANDLE DataLogSent, IOHANDLE DataLogRecv)
{
	if (DataLogSent)
	{
		ms_DataLogSent = DataLogSent;
		dbg_msg("network", "logging sent packages");
	}
	else
		dbg_msg("network", "failed to start logging sent packages");

	if (DataLogRecv)
	{
		ms_DataLogRecv = DataLogRecv;
		dbg_msg("network", "logging recv packages");
	}
	else
		dbg_msg("network", "failed to start logging recv packages");
}

void CNetBase::CloseLog()
{
	if (ms_DataLogSent)
	{
		dbg_msg("network", "stopped logging sent packages");
		io_close(ms_DataLogSent);
		ms_DataLogSent = 0;
	}

	if (ms_DataLogRecv)
	{
		dbg_msg("network", "stopped logging recv packages");
		io_close(ms_DataLogRecv);
		ms_DataLogRecv = 0;
	}
}

void CNetBase::Init()
{
	ms_Huffman.Init();
}