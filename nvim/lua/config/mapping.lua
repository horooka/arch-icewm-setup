local map = vim.keymap.set

-- {"n", "v"}, "p", "paste to a next line"
-- {"n", "v"}, "s", "jump to a entered letter"

--neovide
if vim.g.neovide then
	vim.g.neovide_scale_factor = 1.0
	local change_scale_factor = function(delta)
		vim.g.neovide_scale_factor = vim.g.neovide_scale_factor * delta
	end
	map("n", "<C-=>", function()
		change_scale_factor(1.25)
	end)
	map("n", "<C-->", function()
		change_scale_factor(0.8)
	end)
	map("n", "<C-0>", function()
		vim.g.neovide_scale_factor = 1.0
	end)
end

--file
map({ "n", "i" }, ",q", "<Esc>:q!<CR>", { noremap = true, silent = true })
map({ "n", "i" }, ",e", "<Esc>:wq<CR>", { noremap = true, silent = true })
map({ "n", "i" }, ",w", "<Esc>:w<CR>", { noremap = true, silent = true })
map({ "n", "i", "v" }, ",s", "<Esc>", { noremap = true, silent = true })
map({ "n" }, "<space>m", "<Esc>:make<CR>", { noremap = true, silent = true })
map({ "n" }, "<space>c", "<Esc>:copen<CR>", { noremap = true, silent = true })
--compilation
vim.api.nvim_create_autocmd({ "BufEnter", "BufWinEnter" }, {
	callback = function()
		local ft = vim.bo.filetype
		if ft == "rust" then
			vim.bo.makeprg = "cargo check"
		elseif ft == "c" or ft == "cpp" then
			vim.bo.makeprg = "cmake --build build -j $(nproc)"
		else
			vim.bo.makeprg = ""
		end
	end,
})

-- modes
map({ "n", "i" }, ",v", "<Esc>v", { noremap = true, silent = true })
map({ "n", "i" }, ",t", "<Esc>:!", { noremap = true, silent = true })
map({ "n", "i" }, ",r", "<Esc>:%s/", { noremap = true, silent = true })
map("v", ",r", "<Esc>:'<,'>s/", { noremap = true, silent = false })

-- lines
map({ "n", "i" }, ",b", "<Esc>^i", { noremap = true, silent = true })
map({ "n", "i" }, ",a", "<Esc>A", { noremap = true, silent = true })
map({ "n", "i" }, ",d", "<Esc>Vx", { noremap = true, silent = true })
map({ "n", "i" }, ",c", "<Esc>Vy", { noremap = true, silent = true })

-- tags
map("n", "<space>d", "<C-]>", { desc = "Tag: go to definition" })
map("n", "<space>q", "<C-t>", { desc = "Tag: back" })
-- Shift+k - help for object under cursor
-- map("n", "<space>t", "<C-w>t", { desc = "Tag: open tag file" })
-- map("n", "<space>s", ":tnext<CR>", { desc = "Tag: next match" })
-- map("n", "<space>a", ":tprev<CR>", { desc = "Tag: prev match" })

-- symbols
map("n", ",g", "<cmd>Telescope grep_string<cr>", {
	desc = "Search word under cursor",
})

-- utils
map("n", "<space>h", function()
	local name = vim.fn.input("Header name: ")
	if name == "" then
		return
	end
	local result = vim.system({ vim.fn.expand("text-utils.sh"), "header", name }, { text = true }):wait()
	if result.code ~= 0 then
		vim.notify(("exit code: %s\n%s"):format(result.code, result.stderr or ""), vim.log.levels.ERROR)
		return
	end
	local lines = vim.split(result.stdout or "", "\n", { plain = true })
	if lines[#lines] == "" then
		table.remove(lines)
	end
	if #lines > 0 then
		vim.api.nvim_put(lines, "c", false, true)
	end
end)

map("x", "<space>w", ":'<,'>!text-utils.sh wrap<CR>", { desc = "Wrap selected lines" })
