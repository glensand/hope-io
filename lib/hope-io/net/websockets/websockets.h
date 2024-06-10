#pragma once

#include <array>
#include <functional>
#include <string>

namespace hope::io::websockets {

	std::string generate_handshake(const std::string& host, const std::string& uri);
	bool validate_handshake_response(const void* data, std::size_t length);

	constexpr auto OPCODE_EMPTY = 0x0;

	constexpr auto OPCODE_TEXT = 0x1;
	constexpr auto OPCODE_BINARY = 0x2;

	constexpr auto OPCODE_CLOSE = 0x8;
	constexpr auto OPCODE_PING = 0x9;
	constexpr auto OPCODE_PONG = 0xA;

	struct websocket_frame
	{
		struct header
		{
			std::uint8_t op_code : 4;
			std::uint8_t flags : 3;
			std::uint8_t is_eof : 1;
			std::uint8_t package_length : 7;
			std::uint8_t mask : 1;
		};

		static constexpr auto header_size = sizeof(websocket_frame::header);

		static_assert(sizeof(header) == 0x2, "websocket_frame::header must be 0x2 bytes");

		bool control() const { return header.is_eof && (header.op_code == OPCODE_CLOSE || header.op_code == OPCODE_PING || header.op_code == OPCODE_PONG); }
		bool empty() const { return header.is_eof == 0x0 && header.op_code == OPCODE_EMPTY && length == 0; }

		bool begin_stream() const { return header.is_eof == 0x0 && header.op_code != OPCODE_EMPTY; }
		bool continue_stream() const { return header.is_eof == 0x0 && header.op_code == OPCODE_EMPTY; }
		bool end_stream() const { return header.is_eof && header.op_code == OPCODE_EMPTY; }

		bool complete_stream() const { return header.is_eof && header.op_code != OPCODE_EMPTY; }

		header header{};

		std::array<std::uint8_t, 4> mask;
		std::size_t length{};
	};

	using read_stream_func = std::function<size_t(void*, std::size_t)>;
	websocket_frame read_frame(read_stream_func&& read_function);

}