/* Text is derived from the server state, never from missing cache alone. */
function previewStatusText(status) {
  const count = `${status.completed || 0} / ${status.total || 0} 帧`;
  switch (status.state) {
    case 'running': {
      const phase = status.phase === 'preparing' ? '正在读取绘图元数据' : '正在生成';
      return `快速预览${phase}：${count}（${status.percent || 0}%）`;
    }
    case 'ready': return `快速预览已完成：${count}，可直接播放`;
    case 'failed': return `快速预览生成失败：${count}。${status.message || ''}`;
    case 'interrupted': return `快速预览生成已中断：${count}，当前没有生成进程`;
    case 'partial': return `快速预览尚未完成：${count}，当前没有生成进程`;
    case 'stale': return `快速预览缓存需要更新：${count}`;
    case 'idle': return `快速预览尚未开始生成：${count}`;
    default: return '正在查询快速预览状态…';
  }
}
if (typeof module !== 'undefined') module.exports = {previewStatusText};
