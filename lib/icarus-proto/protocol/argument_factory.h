/*
 * Copyright (C) 2023 Gleb Bezborodov - All Rights Reserved
 */

#pragma once

namespace icarus::io{
    class stream;
}

namespace icarus::proto {

    class argument;

    namespace argument_factory{
        argument *serialize(io::stream &stream);
    }

}
