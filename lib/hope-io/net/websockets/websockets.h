#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace hope::io
{
	class stream;
}

namespace hope::io::websockets {

	std::string generate_handshake(const std::string& host, const std::string& uri);
	bool validate_handshake_response(const void* data, std::size_t length);

	constexpr auto OPCODE_EMPTY = 0x0;

	constexpr auto OPCODE_TEXT = 0x1;
	constexpr auto OPCODE_BINARY = 0x2;

	constexpr auto OPCODE_CLOSE = 0x8;
	constexpr auto OPCODE_PING = 0x9;
	constexpr auto OPCODE_PONG = 0xA;

	constexpr auto OPCODE_UNKNOWN = 0xF;

	enum class opcode_e {
		empty,
		text,
		binary,
		close,
		ping,
		pong
	};

	inline std::uint8_t cast_opcode(opcode_e opcode) {
		switch (opcode)
		{
			case opcode_e::empty: return OPCODE_EMPTY;
			case opcode_e::text: return OPCODE_TEXT;
			case opcode_e::binary: return OPCODE_BINARY;
			case opcode_e::close: return OPCODE_CLOSE;
			case opcode_e::ping: return OPCODE_PING;
			case opcode_e::pong: return OPCODE_PONG;
		}
		return OPCODE_UNKNOWN;
	}

#pragma pack(push, 2)
	struct websocket_frame
	{
		struct header_t
		{
			std::uint8_t op_code : 4;
			std::uint8_t flags : 3;
			std::uint8_t is_eof : 1;
			std::uint8_t package_length : 7;
			std::uint8_t mask : 1;
		};

		static constexpr auto header_size = sizeof(header_t);
		static_assert(header_size == 0x2u, "websocket_frame::header must be 0x2 bytes");

		bool control() const { return header.is_eof && (header.op_code == OPCODE_CLOSE || header.op_code == OPCODE_PING || header.op_code == OPCODE_PONG); }
		bool empty() const { return header.is_eof == 0x0u && header.op_code == OPCODE_EMPTY && length == 0; }

		bool begin_stream() const { return header.is_eof == 0x0u && header.op_code != OPCODE_EMPTY; }
		bool continue_stream() const { return header.is_eof == 0x0u && header.op_code == OPCODE_EMPTY; }
		bool end_stream() const { return header.is_eof && header.op_code == OPCODE_EMPTY; }

		bool complete_stream() const { return header.is_eof && (header.op_code == OPCODE_TEXT || header.op_code == OPCODE_BINARY); }

		bool ping() const { return header.op_code == OPCODE_PING; }

		bool masked() const { return header.mask != 0x0u; }

		header_t header{};
		std::array<std::uint8_t, 4> mask{};
		std::size_t length{};
	};
#pragma pack(pop)

	websocket_frame read_frame(stream* stream);
	size_t read_data(const websocket_frame& frame, stream* stream, std::string& out_data);

	std::string generate_package(const std::string& data, opcode_e code, bool is_eof, bool masked);
}
