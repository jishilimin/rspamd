--[[
Copyright (c) 2019, Vsevolod Stakhov <vsevolod@highsecure.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
]]--

--[[[
-- @module lua_magic
-- This module contains file types detection logic
--]]

local patterns = require "lua_magic/patterns"
local types = require "lua_magic/types"
local fun = require "fun"
local lua_util = require "lua_util"

local rspamd_text = require "rspamd_text"
local rspamd_trie = require "rspamd_trie"

local N = "lua_magic"
local exports = {}
-- trie objects
local compiled_patterns
local compiled_short_patterns
local compiled_tail_patterns
-- {<str>, <match_object>, <pattern_object>} indexed by pattern number
local processed_patterns = {}
local short_patterns = {}
local tail_patterns = {}

local short_match_limit = 128
local max_short_offset = -1
local min_tail_offset = math.huge

local function process_patterns(log_obj)
  -- Add pattern to either short patterns or to normal patterns
  local function add_processed(str, match, pattern)
    if match.position and type(match.position) == 'number' then
      if match.tail then
        -- Tail pattern
        tail_patterns[#tail_patterns + 1] = {
          str, match, pattern
        }
        if min_tail_offset > match.tail then
          min_tail_offset = match.tail
        end

        lua_util.debugm(N, log_obj, 'add tail pattern %s for ext %s',
            str, pattern.ext)
      elseif match.position < short_match_limit then
        short_patterns[#short_patterns + 1] = {
          str, match, pattern
        }
        lua_util.debugm(N, log_obj, 'add short pattern %s for ext %s',
            str, pattern.ext)

        if max_short_offset < match.position then
          max_short_offset = match.position
        end
      else
        processed_patterns[#processed_patterns + 1] = {
          str, match, pattern
        }

        lua_util.debugm(N, log_obj, 'add long pattern %s for ext %s',
            str, pattern.ext)
      end
    else
      processed_patterns[#processed_patterns + 1] = {
        str, match, pattern
      }

      lua_util.debugm(N, log_obj, 'add long pattern %s for ext %s',
          str, pattern.ext)
    end
  end

  if not compiled_patterns then
    for ext,pattern in pairs(patterns) do
      assert(types[ext], 'not found type: ' .. ext)
      pattern.ext = ext
      for _,match in ipairs(pattern.matches) do
        if match.string then
          if match.relative_position and not match.position then
            match.position = match.relative_position + #match.string
          end
          add_processed(match.string, match, pattern)
        elseif match.hex then
          local hex_table = {}

          for i=1,#match.hex,2 do
            local subc = match.hex:sub(i, i + 1)
            hex_table[#hex_table + 1] = string.format('\\x{%s}', subc)
          end

          if match.relative_position and not match.position then
            match.position = match.relative_position + #match.hex / 2
          end
          add_processed(table.concat(hex_table), match, pattern)
        end
      end
    end

    compiled_patterns = rspamd_trie.create(fun.totable(
        fun.map(function(t) return t[1] end, processed_patterns)),
        rspamd_trie.flags.re
    )
    compiled_short_patterns = rspamd_trie.create(fun.totable(
        fun.map(function(t) return t[1] end, short_patterns)),
        rspamd_trie.flags.re
    )
    compiled_tail_patterns = rspamd_trie.create(fun.totable(
        fun.map(function(t) return t[1] end, tail_patterns)),
        rspamd_trie.flags.re
    )

    lua_util.debugm(N, log_obj,
        'compiled %s (%s short; %s long; %s tail) patterns',
        #processed_patterns + #short_patterns + #tail_patterns,
        #short_patterns, #processed_patterns, #tail_patterns)
  end
end

local function match_chunk(input, tlen, offset, trie, processed_tbl, log_obj, res)
  local matches = trie:match(input)

  local last = tlen

  local function add_result(match, pattern)
    if not res[pattern.ext] then
      res[pattern.ext] = 0
    end
    if match.weight then
      res[pattern.ext] = res[pattern.ext] + match.weight
    else
      res[pattern.ext] = res[pattern.ext] + 1
    end

    lua_util.debugm(N, log_obj,'add pattern for %s, weight %s, total weight %s',
        pattern.ext, match.weight, res[pattern.ext])
  end

  for npat,matched_positions in pairs(matches) do
    local pat_data = processed_tbl[npat]
    local pattern = pat_data[3]
    local match = pat_data[2]

    local function match_position(pos, expected)
      local cmp = function(a, b) return a == b end
      if type(expected) == 'table' then
        -- Something like {'>', 0}
        if expected[1] == '>' then
          cmp = function(a, b) return a > b end
        elseif expected[1] == '>=' then
          cmp = function(a, b) return a >= b end
        elseif expected[1] == '<' then
          cmp = function(a, b) return a < b end
        elseif expected[1] == '<=' then
          cmp = function(a, b) return a <= b end
        elseif expected[1] == '!=' then
          cmp = function(a, b) return a ~= b end
        end
        expected = expected[2]
      end

      -- Tail match
      if expected < 0 then
        expected = last + expected + 1
      end
      return cmp(pos, expected)
    end
    -- Single position
    if match.position then
      local position = match.position

      for _,pos in ipairs(matched_positions) do
        lua_util.debugm(N, log_obj, 'found match %s at offset %s(from %s)',
            pattern.ext, pos, offset)
        if match_position(pos + offset, position) then
          add_result(match, pattern)
          break
        end
      end
    end
    -- Match all positions
    if match.positions then
      local all_right = true
      for _,position in ipairs(match.positions) do
        local matched = false
        for _,pos in ipairs(matched_positions) do
          if not match_position(pos + offset, position) then
            matched = true
            break
          end
        end
        if not matched then
          all_right = false
          break
        end
      end

      if all_right then
        add_result(match, pattern)
      end
    end
  end
end

local function process_detected(res)
  local extensions = lua_util.keys(res)

  if #extensions > 0 then
    table.sort(extensions, function(ex1, ex2)
      return res[ex1] > res[ex2]
    end)

    return extensions,res[extensions[1]]
  end

  return nil
end

exports.detect = function(input, log_obj)
  if not log_obj then log_obj = rspamd_config end
  process_patterns(log_obj)

  local res = {}

  if type(input) == 'string' then
    -- Convert to rspamd_text
    input = rspamd_text.fromstring(input)
  end


  if type(input) == 'userdata' then
    local inplen = #input

    -- Check tail matches
    if inplen > min_tail_offset then
      local tail = input:span(inplen - min_tail_offset, min_tail_offset)
      match_chunk(tail, inplen, inplen - min_tail_offset,
          compiled_tail_patterns, tail_patterns, log_obj, res)
    end

    -- Try short match
    local head = input:span(1, math.min(max_short_offset, inplen))
    match_chunk(head, inplen, 0,
        compiled_short_patterns, short_patterns, log_obj, res)

    -- Check if we have enough data or go to long patterns
    local extensions,confidence = process_detected(res)

    if extensions and #extensions > 0 and confidence > 30 then
      -- We are done on short patterns
      return extensions[1],types[extensions[1]]
    end

    -- No way, let's check data in chunks or just the whole input if it is small enough
    if #input > exports.chunk_size * 3 then
      -- Chunked version as input is too long
      local chunk1, chunk2 =
      input:span(1, exports.chunk_size * 2),
      input:span(inplen - exports.chunk_size, exports.chunk_size)
      local offset1, offset2 = 0, inplen - exports.chunk_size

      match_chunk(chunk1, inplen,
          offset1, compiled_patterns, processed_patterns, log_obj, res)
      match_chunk(chunk2, inplen,
          offset2, compiled_patterns, processed_patterns, log_obj, res)
    else
      -- Input is short enough to match it at all
      match_chunk(input, inplen, 0,
          compiled_patterns, processed_patterns, log_obj, res)
    end
  else
    -- Table input is NYI
    assert(0, 'table input for match')
  end

  local extensions = process_detected(res)

  if extensions and #extensions > 0 then
    return extensions[1],types[extensions[1]]
  end

  -- Nothing found
  return nil
end

-- This parameter specifies how many bytes are checked in the input
-- Rspamd checks 2 chunks at start and 1 chunk at the end
exports.chunk_size = 32768

return exports