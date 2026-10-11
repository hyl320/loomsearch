# 已理解 tokenizer 差异（ASCII vs unicode61）

用户能正确判断「`café thread` 文件查 `cafe`」在 ASCII 分词下不命中、在 unicode61 下命中，并能说出原因是「是否去重音」。这说明「ASCII 分词无法处理非 ASCII/重音/CJK，unicode61 按 Unicode 规则分词并去重音」这一层已建立。后续不必再讲 tokenizer 基础，可直接用它解释中文分词缺口和 benchmark `parity_mismatches` 的边界条件。
